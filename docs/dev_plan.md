# SiriusScope — план разработки high-load версии

**Цель документа:** зафиксировать ближайший и полный план переработки SiriusScope в реальное high-load изделие, рассчитанное на честный входной поток не менее **90 MB/s**, предсказуемые задержки, контролируемый backpressure и корректное отображение/анализ данных.

Документ составлен после перехода runtime БЦО на `HighLoadSimulatorBcoStreamSource`, внедрения нового `src/pipeline` data plane, `WaterfallAggregator`, `SpectrumAggregator`, `BearingAggregator` и ручного тестирования, которое выявило проблемы live/history водопада, итоговой таблицы и расчёта ППИ/ДИ.

---

## 1. Текущее состояние проекта

### Уже сделано

В проекте уже появилась правильная основа high-load архитектуры:

```text
HighLoadSimulatorBcoStreamSource
    -> DataIngestPipeline
    -> SignalBlockPool
    -> BoundedBlockQueue
    -> ProcessingEngine
    -> WaterfallAggregator
    -> SpectrumAggregator
    -> BearingAggregator
    -> Snapshot adapters
    -> Qt/QML
```

Старые high-load raw paths отключены:

- `WaterfallController` больше не собирает `pendingSamples`;
- `RealtimeSignalPipeline` не используется как production high-load path;
- `SignalSampleBus` и `BearingFrameBus` не получают high-load raw vectors;
- `SpectrumEnvelopeWorker` не получает копии high-load blocks;
- `ScanController` получает `BearingSnapshot` summary, а не raw `SignalSample`.

Это правильное направление: Qt/QML постепенно становится **control/presentation layer**, а не hot data path.

### Что пока не готово

Несмотря на архитектурный прогресс, текущая реализация ещё не доказывает полную high-load пропускную способность.

Критичные ограничения:

1. `DataIngestPipeline` использует bounded queue и может отбрасывать блоки.
2. `SignalBlockPool` ограничен числом блоков и может исчерпываться.
3. `ProcessingEngine` пока однопоточный.
4. `SignalBlock::assignSamples()` копирует данные из `BcoSampleBlock::samples`.
5. `HighLoadSimulatorBcoStreamSource` вызывает callback синхронно, поэтому callback может замедлять сам генератор.
6. `WaterfallSnapshot` публикуется через latest-only exchange, поэтому временные строки водопада могут теряться до того, как UI их заберёт.
7. Live-водопад и history-водопад используют разные фактические semantics отображения.
8. ППИ/ДИ больше не считаются, потому что старый raw `SignalParameterAccumulator` отключён от high-load потока.
9. Итоговая таблица создаёт несколько строк на один `BandItem`, если есть несколько частотных bins.
10. Текущий `RealBcoEquivalent` не является строгим доказательством потока 90 MB/s.

---

## 2. Главные принципы дальнейшей разработки

### 2.1. Не возвращать MVP-path

Запрещено возвращать старую архитектуру как production path:

```text
High-load source -> WaterfallController -> RealtimeSignalPipeline -> Qt buses
```

Нужно развивать только новую архитектуру:

```text
High-load source -> Data plane -> Aggregators -> Snapshots/Summaries -> Qt/QML
```

### 2.2. Data plane и control plane должны быть разделены

**Data plane:**

- ingest;
- block pool;
- queues;
- processing engine;
- DSP/aggregators;
- storage writer;
- metrics;
- overload policy.

**Control/presentation plane:**

- QML;
- настройки BandItem;
- start/stop;
- статусы;
- ResultTable;
- визуализация snapshots;
- команды антенны/БЦО.

Qt может управлять pipeline, но не должен переносить raw поток.

### 2.3. Водопад — это временная лента, а не latest snapshot

Для спектра и статуса подходит latest-only snapshot.

Для водопада latest-only snapshot **не подходит**, потому что водопад — это последовательность строк во времени. Если промежуточные строки потеряны, live и history начинают показывать разную картину.

### 2.4. 90 MB/s нужно определить в байтах входного потока

Нельзя считать “90 MB/s” через `sizeof(core::SignalSample)` без явного контракта.

Нужно определить:

```text
90 MB/s raw BCO input
или
90 MB/s parsed SignalSample payload
или
90 MB/s internal SignalBlock memory traffic
```

Для изделия главный критерий должен быть:

```text
inputBytesPerSecond >= 90_000_000
```

а не только:

```text
samplesPerSecond = N
```

---

## 3. P0 — исправить прямо сейчас

Цель P0: стабилизировать уже реализованный high-load runtime, чтобы ручное тестирование перестало показывать рассинхрон водопада, потерю строк, отсутствие ППИ/ДИ и дубли в итоговой таблице.

---

### P0.1. Заменить latest-only WaterfallSnapshotExchange на Waterfall row queue

**Проблема:**  
`SnapshotExchange<WaterfallSnapshot>` хранит только последний snapshot. Если data plane произвёл несколько строк между двумя polling-итерациями UI, старые строки теряются.

**Симптом:**  
Во время записи данные выглядят как непрерывная полоса, но после прокрутки назад/вперёд становятся пунктиром.

**Нужно сделать:**

Ввести отдельный механизм для водопада:

```cpp
class WaterfallRowQueue
{
public:
    void pushRows(std::vector<WaterfallSnapshotRow> rows);
    std::vector<WaterfallSnapshotRow> drain(std::size_t maxRows);
    WaterfallRowQueueMetrics metrics() const;
};
```

Или:

```cpp
class WaterfallSnapshotQueue
{
public:
    void publish(std::shared_ptr<const WaterfallSnapshot> snapshot);
    std::vector<std::shared_ptr<const WaterfallSnapshot>> drainAfter(std::uint64_t sequenceId);
};
```

**Требования:**

- не терять строки между polling-итерациями;
- иметь bounded capacity;
- считать dropped rows/snapshots;
- не блокировать processing thread надолго;
- дать `WaterfallController` возможность забрать пачку строк за один тик.

**Acceptance criteria:**

- если `ProcessingEngine` произвёл rows `1..100`, а UI poll был только один раз после row 100, storage/live должны получить все 100 rows или явно зафиксировать counted drops;
- `waterfallProducedRows == waterfallDeliveredRows + waterfallDroppedRows`;
- live/history отображают одну и ту же временную структуру.

---

### P0.2. Единая временная база водопада

**Проблема:**  
Горизонтальная сетка водопада бежит быстрее данных. Это указывает на рассинхрон между:

- `HighLoadSimulatorBcoStreamSource::sampleIndex`;
- `BcoStreamConfig.timeBase.samplePeriodNs`;
- `WaterfallAggregatorConfig.rowPeriodNs`;
- `WaterfallTimelineViewport.rowPeriodMs`;
- реальным темпом генерации.

**Нужно сделать:**

Зафиксировать контракт:

```text
sampleIndex -> TimeBase -> utcNs -> rowPeriodMs -> timeline slot
```

Единый источник правды:

```cpp
core::TimeBase {
    recordingStartUtcNs,
    firstSampleIndex,
    samplePeriodNs
}
```

**Задачи:**

1. Проверить, что `HighLoadSimulatorBcoStreamSource` увеличивает `sampleIndex` в соответствии с `timeBase.samplePeriodNs`.
2. Проверить, что `samplesPerSecond` симулятора и `samplePeriodNs` не противоречат друг другу.
3. Добавить диагностику:
   - `waterfallRowUtcDeltaMinMs`;
   - `waterfallRowUtcDeltaMaxMs`;
   - `waterfallExpectedRowPeriodMs`;
   - `waterfallTimebaseMismatchWarnings`.
4. Добавить тесты:
   - соседние rows имеют delta `rowPeriodMs`;
   - `utcMs` монотонен;
   - `sampleIndex` за 1 секунду соответствует ожидаемой скорости.

**Acceptance criteria:**

- сетка времени в live не убегает относительно данных;
- при history-прокрутке те же строки оказываются в тех же временных слотах;
- row period в UI равен row period из data plane.

---

### P0.3. Сделать live/history consistency для водопада

**Проблема:**  
Live-режим просто делает `pushLine()`, а history раскладывает строки через временной mapper. Поэтому live может визуально сжимать временные пропуски, а history показывает их как пустые места.

**Нужно сделать:**

Live-режим должен использовать ту же time-slot модель, что и history.

Вместо логики:

```text
new row -> pushLine(row)
```

нужно:

```text
new row -> calculate slot by row.utcMs -> write slot
```

или, если сохраняется ring buffer push model:

```text
если между lastRowUtcMs и newRowUtcMs есть пропущенные rowPeriodMs,
вставить пустые rows
```

**Acceptance criteria:**

- live-изображение и history-изображение совпадают;
- если data plane даёт пунктир, он виден пунктиром сразу;
- если data plane даёт непрерывную полосу, она остаётся непрерывной после прокрутки истории.

---

### P0.4. Вернуть расчёт ППИ/ДИ через data plane

**Проблема:**  
ППИ и ДИ всегда `Н/Д`, потому что `SignalParameterAccumulator` больше не получает raw samples через `SignalSampleBus`. Возвращать raw samples в `ScanController` нельзя.

**Нужно сделать:**

Добавить компонент:

```text
src/pipeline/signal_parameter_aggregator.*
src/pipeline/signal_parameter_snapshot.*
```

Поток:

```text
SignalBlock
    -> SignalParameterAggregator
    -> SignalParameterSnapshot
    -> SignalParameterSnapshotAdapter
    -> ScanController
    -> ResultTable
```

**Минимальная модель:**

```cpp
struct BandSignalParametersSummary
{
    int bandIndex = 0;
    std::vector<std::int64_t> frequenciesHz;
    std::optional<double> pulseRepetitionPeriodUs;
    std::optional<double> pulseWidthUs;
    std::uint64_t pulseCount = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
};
```

**Требования:**

- работать в `ProcessingEngine`, а не в Qt layer;
- не публиковать raw samples;
- считать параметры по `bandIndex`;
- поддержать импульсные данные симулятора;
- выдавать summary в `ScanController`.

**Acceptance criteria:**

- после сканирования ResultTable показывает ППИ/ДИ вместо `Н/Д`, если поток содержит валидные импульсы;
- `ScanController::finalizeCompletedScan()` использует data-plane summaries, а не пустой старый accumulator;
- raw samples не возвращаются в `SignalSampleBus`.

---

### P0.5. Итоговая таблица: одна строка на BandItem

**Проблема:**  
После сканирования появляется по две или больше строк на один `BandItem`, если `BearingAggregator` дал несколько frequency bins.

**Правильное поведение для текущей версии:**  
Одна строка на один `BandItem`, внутри строки — список всех частот, принятых в этой полосе во время сканирования.

**Нужно сделать:**

Добавить агрегатор итоговых scan results:

```text
ScanBandResultAggregator
```

Он должен объединять `BearingEstimate`/`BearingResult` по `bandIndex`.

**Логика:**

```text
input:
    bandIndex + frequencyHz + bearingAzimuthDeg + quality + sampleIndex

output:
    one BearingResult per bandIndex
    frequenciesHz = unique sorted list
    bearingAzimuthDeg = weighted/circular mean
    quality = max или weighted average
```

**Важно:**

- для live bearing marks можно показывать несколько оценок;
- для итоговой таблицы нужно агрегировать до уровня `BandItem`.

**Acceptance criteria:**

- если один `BandItem` содержит несколько частот, в таблице одна строка;
- поле частот содержит список частот;
- пеленг строки рассчитан устойчиво;
- дубликаты частот не создают новые строки.

---

### P0.6. Добавить high-load throughput audit test

**Проблема:**  
Сейчас нет теста, который доказывает, что SiriusScope честно принимает и обрабатывает весь high-load поток без drops.

**Нужно сделать:**

Добавить тестовый/benchmark target:

```text
tests/perf/tst_high_load_data_plane.cpp
или
tools/pipeline_benchmark/
```

**Минимальный сценарий:**

```text
HighLoadSimulatorBcoStreamSource
    -> DataIngestPipeline
    -> ProcessingEngine
```

Запуск на 60 секунд для CI/local и на 30 минут для ручного stress test.

**Метрики каждую секунду:**

- source produced samples/s;
- source equivalent MB/s;
- max callback duration;
- pipeline input samples/s;
- pipeline processed samples/s;
- dropped samples;
- queue depth;
- queue dropped blocks;
- block pool usage;
- block pool exhausted;
- max block age;
- max processing latency;
- produced waterfall rows;
- delivered waterfall rows.

**Acceptance criteria для 60-секундного smoke test:**

```text
droppedSamples == 0
queueDroppedBlocks == 0
blockPoolExhausted == 0
processedSamples == inputSamples
source producedSamplesPerSecond стабилен
maxBlockAgeMs bounded
```

**Acceptance criteria для 30-минутного stress test:**

```text
нет OOM
нет устойчивого роста queueDepth
нет роста blockAge
нет drops
GUI остаётся отзывчивым
live/history не расходятся
```

---

## 4. P1 — полная high-load пропускная способность

Цель P1: перейти от “архитектура стала правильной” к “система доказанно держит поток 90 MB/s”.

---

### P1.1. Честный симулятор 90 MB/s

**Проблема:**  
Текущий `RealBcoEquivalent` задаётся через `samplesPerSecond`, но это не гарантирует ровно 90 MB/s raw input. Кроме того, callback может замедлять генератор.

**Нужно сделать новый режим симулятора:**

```text
TargetRawThroughput90MBps
```

Но не как UI-selectable profile, а как production/perf default для high-load тестов.

**Главный контракт:**

```cpp
struct ThroughputTarget
{
    std::uint64_t targetBytesPerSecond = 90'000'000;
    std::chrono::milliseconds batchPeriod{10};
    PayloadAccountingMode mode = PayloadAccountingMode::RawBcoBytes;
};
```

**Симулятор должен считать:**

```text
bytesPerBatch = targetBytesPerSecond * batchPeriodSeconds
```

а не только:

```text
samplesPerSecond * batchPeriod
```

**Нужно определить raw packet model:**

```cpp
struct SimulatedBcoPacketHeader
{
    ...
};

struct SimulatedBcoSampleRecord
{
    ...
};
```

Даже если физический протокол БЦО пока не реализован, нужна честная accounting-модель:

```text
packet header bytes
sample record bytes
metadata bytes
padding/alignment
```

**Два режима accounting:**

1. `RawBcoBytes` — для проверки 90 MB/s входного потока.
2. `ParsedSignalSampleBytes` — для оценки внутренней нагрузки после парсинга.

**Важно:**

- source metrics должны показывать `targetBytesPerSecond`, `producedRawBytesPerSecond`, `producedParsedSamplesPerSecond`;
- если callback тормозит, симулятор не должен молча снижать target throughput;
- нужно фиксировать `scheduleLagMs`, `missedBatchDeadlines`, `simulatorBackpressureEvents`.

**Acceptance criteria:**

- за 60 секунд produced raw bytes ~= 90 MB/s ± 5%;
- если pipeline не успевает, это видно как drops/backpressure, а не как молчаливое снижение скорости генерации;
- source metrics отделяют raw bytes от internal `SignalSample` bytes.

---

### P1.2. Direct pool fill без промежуточной копии

**Проблема:**  
Сейчас source создаёт `BcoSampleBlock::samples`, а `DataIngestPipeline::ingestSamples()` копирует samples в `SignalBlock`.

**Нужно сделать:**

Перевести source на заполнение блока из pool:

```text
SignalBlockHandle block = pool.acquire();
source.fill(block.mutableSamplesBuffer());
queue.push(std::move(block));
```

Или создать ingest API:

```cpp
core::OperationResult ingestBlock(SignalBlockHandle block);
```

**Требования:**

- убрать `std::vector::assign` в hot path;
- заранее выделять память;
- не делать heap allocation на каждый batch;
- сохранять metadata: sequenceId, sampleIndex range, producedAt, antennaAzimuthDeg.

**Acceptance criteria:**

- на hot path нет полной копии `BcoSampleBlock::samples -> SignalBlock::samples`;
- benchmark показывает снижение CPU/memory bandwidth;
- все старые тесты pipeline проходят.

---

### P1.3. Полноценный DSP/thread pool

**Проблема:**  
`ProcessingEngine` пока использует один worker thread. Для полной high-load обработки нужен пул.

**Целевая модель потоков:**

```text
1 RX / ingest thread
N DSP workers
1 aggregation/finalization thread или deterministic merge stage
1 storage writer thread
1 GUI thread
```

Где:

```text
N = max(1, physical_cores - reserved_threads)
```

Обычно:

```text
reserved_threads = 3
    1 ingest
    1 storage
    1 GUI/control
```

**Нужно реализовать:**

```text
ProcessingEngine
    inputQueue
    worker pool
    per-worker temporary buffers
    ordered/merge output stage
    metrics
```

**Важный вопрос порядка:**

Для водопада и signal parameters порядок временных windows важен.

Варианты:

1. Partition by time blocks and merge by sequenceId.
2. Single aggregator thread receives worker results in order.
3. Per-worker partial aggregates + ordered reducer.

**Рекомендуемая схема:**

```text
RX
  -> block queue
  -> DSP workers produce ProcessingBlockResult
  -> ordered reducer by sequenceId/windowIndex
  -> snapshot/row queues
```

**Acceptance criteria:**

- число worker threads конфигурируется;
- блоки обрабатываются параллельно;
- итоговые waterfall rows остаются временно упорядоченными;
- нет data races;
- queue depth не растёт при 90 MB/s;
- processed MB/s >= input MB/s.

---

### P1.4. Очереди: от mutex queue к hot-path queue

**Проблема:**  
Текущая `BoundedBlockQueue` использует `std::mutex`, `std::condition_variable`, `std::deque`. Это допустимо для первого этапа, но не гарантирует минимальные задержки под 90 MB/s.

**Нужно сделать:**

Оставить текущий API, но заменить реализацию hot queue на одну из моделей:

- SPSC ring buffer для `RX -> dispatcher`;
- MPMC bounded queue для `dispatcher -> workers`;
- per-worker SPSC queues + work stealing только если потребуется.

**Требования:**

- bounded capacity;
- metrics;
- explicit overload policy;
- no unbounded memory growth.

**Acceptance criteria:**

- API верхних слоёв не ломается;
- perf test показывает снижение contention;
- drops фиксируются явно.

---

### P1.5. Memory pool production level

**Проблема:**  
Текущий pool уже есть, но внутри блока всё ещё `std::vector`, и размер/число блоков пока не привязаны к 90 MB/s.

**Нужно сделать:**

Ввести production memory policy:

```cpp
struct MemoryPoolSizing
{
    std::uint64_t targetBytesPerSecond;
    std::chrono::milliseconds maxPipelineLatency;
    std::size_t blockPayloadBytes;
    std::size_t safetyMultiplier;
};
```

Расчёт:

```text
requiredBlocks = targetBytesPerSecond * maxPipelineLatencySeconds / blockPayloadBytes * safetyMultiplier
```

Например:

```text
90 MB/s
target max pipeline latency 500 ms
block 1 MB
safety x2
=> минимум 90 blocks
```

**Требования:**

- фиксированный размер block payload;
- no vector growth;
- counters: acquired, released, exhausted, high watermark;
- diagnostics при pool usage > 80%;
- fail test при pool exhausted в нормальном режиме.

---

### P1.6. Backpressure policy

**Проблема:**  
Сейчас drops возможны, но политика не формализована.

**Нужно выбрать режимы:**

1. `LosslessRequired` — для записи/анализа, если поток нельзя терять.
2. `RealtimeBestEffort` — для GUI, можно decimate/drop visualization rows.
3. `RawStoragePriority` — при перегрузке писать raw, снижать GUI.
4. `GuiDecimation` — уменьшать FPS/rows при перегрузке.

**Для SiriusScope рекомендуемый порядок при перегрузке:**

```text
1. Снизить GUI refresh / snapshot publish rate.
2. Decimate spectrum.
3. Decimate waterfall visualization, но не raw storage.
4. Если storage не успевает — фиксировать data loss как critical diagnostic.
5. Никогда не скрывать drops.
```

**Acceptance criteria:**

- все drops имеют counters;
- UI показывает overload status;
- diagnostics rate-limited;
- нет silent data loss.

---

## 5. P2 — StoragePipeline и долговременная запись

Цель P2: писать high-load данные и результаты без блокировки ingest/processing.

---

### P2.1. StoragePipeline

Схема:

```text
ProcessingEngine / Ingest
    -> StorageQueue
    -> StorageWriterThread
    -> chunked binary files
    -> metadata/index
```

**Raw/near-raw storage:**

```text
session/
    raw_000001.bin
    raw_000002.bin
    raw_index.bin
    metadata.json
```

**Result storage:**

```text
session/
    waterfall_rows.bin
    spectrum_snapshots.bin
    bearing_results.bin
    signal_parameters.bin
```

**Требования:**

- append-only;
- chunked;
- отдельный writer thread;
- bounded queue;
- storage latency metrics;
- write throughput metrics;
- no writes from GUI thread.

---

### P2.2. Waterfall storage consistency

После P0.1/P0.3 storage должен сохранять ровно те rows, которые видны в live.

**Acceptance criteria:**

```text
live rows == stored rows == history rows
```

С учётом:

```text
deliveredRows + droppedRows == producedRows
```

---

## 6. P3 — SIMD и оптимизация DSP

Этап начинать только после стабилизации semantics и proof-of-throughput.

Возможные направления:

- SIMD для amplitude aggregation;
- flat arrays вместо map/set;
- предрасчёт frequency bin lookup;
- per-band contiguous buffers;
- branch reduction в hot loop;
- cache-local temporary buffers per worker;
- optional xsimd/Eigen/IPP.

---

## 7. Рекомендуемый порядок PR

### PR-01: Waterfall delivery queue

- заменить latest-only waterfall exchange на queue/drain;
- добавить delivered/dropped row counters;
- обновить tests;
- исправить live/history loss.

### PR-02: Waterfall timebase consistency

- зафиксировать контракт `sampleIndex -> TimeBase -> utcMs`;
- добавить diagnostics и tests;
- исправить убегающую сетку.

### PR-03: Live/history unified rendering

- live рендерит rows через timeline slots;
- пропуски не сжимаются;
- history и live совпадают.

### PR-04: SignalParameterAggregator

- ППИ/ДИ в data plane;
- `SignalParameterSnapshotAdapter`;
- `ScanController` получает summary;
- ResultTable снова показывает ППИ/ДИ.

### PR-05: Final scan result aggregation by BandItem

- одна строка на BandItem;
- список частот;
- weighted/circular bearing aggregation.

### PR-06: High-load throughput audit tests

- 60-sec local/CI smoke;
- 30-min manual stress profile;
- metrics report.

### PR-07: Honest 90 MB/s simulator

- target raw bytes/s;
- raw packet accounting;
- schedule lag metrics;
- no silent slowdown.

### PR-08: Direct pool fill

- убрать промежуточную копию;
- source заполняет pooled block;
- benchmark до/после.

### PR-09: ProcessingEngine thread pool

- worker pool;
- ordered reducer;
- per-worker buffers;
- metrics.

### PR-10: Production queue/memory policy

- SPSC/MPMC bounded queues;
- pool sizing;
- overload policy.

### PR-11: StoragePipeline

- storage thread;
- chunked binary files;
- metadata/index;
- throughput metrics.

---

## 8. Definition of Done для high-load изделия

SiriusScope можно считать готовым к high-load runtime только если выполнены все критерии:

### Throughput

```text
inputRawBytesPerSecond >= 90_000_000
processedBytesPerSecond >= inputRawBytesPerSecond
droppedSamples == 0 в lossless режиме
queueDroppedBlocks == 0
blockPoolExhausted == 0
```

### Latency

```text
maxBlockAgeMs bounded
processingLatencyMaxMs bounded
storageLatencyMaxMs bounded
GUI remains responsive
```

### Waterfall

```text
live == history
no silent row loss
producedRows == deliveredRows + droppedRows
time grid matches data
```

### Spectrum

```text
spectrum uses snapshots only
no raw samples in Qt/QML
snapshot FPS bounded
```

### Bearing

```text
bearing visible during scan
final results aggregated by BandItem
missing beam diagnostics aggregated
```

### Signal parameters

```text
ППИ/ДИ считаются в data plane
ResultTable получает parameters summary
no raw SignalSample bus delivery
```

### Simulator

```text
90 MB/s measured by raw byte accounting
source does not silently slow down under callback pressure
scheduleLagMs and missedDeadlines visible
```

### Architecture

```text
Qt/QML = control/presentation plane
pipeline/core/hardware/storage = data plane
no high-load QVector/QByteArray/std::vector payloads through Qt queued connections
```

---

## 9. Что не делать сейчас

Не тратить время до P0/P1 на:

- сложную историю StoragePipeline, пока live/history semantics неправильные;
- SIMD, пока нет честного throughput benchmark;
- lock-free rewrite без измеренного bottleneck;
- новый красивый renderer, пока row delivery теряет данные;
- расширенную классификацию РТС;
- multi-target tracking;
- попытки вернуть raw samples в `ScanController`.

---

## 10. Главный ближайший фокус

Ближайший этап должен называться:

```text
High-load runtime stabilization: time, row delivery, scan summaries, throughput proof
```

Ключевой результат:

```text
SiriusScope честно принимает high-load поток,
не теряет строки водопада без счётчиков,
одинаково показывает live/history,
считает ППИ/ДИ в data plane,
агрегирует итоговую таблицу по BandItem,
и имеет тест, доказывающий пропускную способность.
```
