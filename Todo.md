# To do list

[ ] Сделать "Глубокое" нажатие на кнопки поворота антенны

/
├── AGENTS.md
├── README.md
├── CONTEXT.md              # временно оставить, чтобы не ломать CMake
├── ARCHITECTURE.md         # временно оставить, чтобы не ломать CMake
│
└── docs/
    ├── README.md
    │
    ├── spec/
    │   ├── scope.md        # из CONTEXT.md, но скорректировать
    │   └── glossary.md     # новый файл
    │
    ├── architecture/
    │   ├── layers.md       # из ARCHITECTURE.md, но скорректировать
    │   └── data-flow.md    # новый короткий файл
    │
    └── development/
        └── build-and-test.md
		

Ты работаешь в репозитории SiriusScope.

Задача: исправить расчёт параметров импульсного сигнала так, чтобы для режима “Генератор” рассчитанные значения ППИ/PRI и ДИ/PW в итоговой таблице сходились с настройками BandItem.

Проблема:
Сейчас при настройках генератора:
- PRI = 100000 мкс
- PW = 10000 мкс

в итоговой таблице могут появляться значения примерно:
- ППИ ≈ 0,943 мкс
- ДИ ≈ 0,32 мкс

Это неверно. При `core::DomainConstraints::defaultSamplePeriodNs = 320 ns` ожидается примерно:
- PRI ≈ 100000 мкс
- PW ≈ 10000 мкс

Причина:
`SignalParameterEstimator` сейчас группирует отсчёты в импульс только если соседние `sampleIndex` отличаются не больше чем на `maxIntraPulseGapSamples`, по умолчанию 1. Но текущий `SimulatorBcoSampleSource` генерирует разреженные точки по каждому `bandIndex`, потому что циклически проходит по источникам сцены. Поэтому estimator воспринимает почти каждый отдельный sample как отдельный импульс длительностью 1 sample, то есть 0,32 мкс.

Нужно исправить модель так, чтобы расчёт параметров сходился с настройками генератора.

---

## Главное требование

Не подставлять PRI/PW напрямую из настроек BandItem в итоговую таблицу.

Параметры должны по-прежнему рассчитываться из принятых `core::SignalSample`, но алгоритм генерации и/или группировки должен быть согласован с импульсной природой сигнала.

---

# 1. Добавить generator-aware конфигурацию estimator

Файлы:

```text
src/processing/signal_parameter_estimator.h
src/processing/signal_parameter_estimator.cpp
````

Расширить `SignalParameterEstimatorConfig`.

Сейчас есть:

```cpp
struct SignalParameterEstimatorConfig
{
    std::uint64_t samplePeriodNs = core::DomainConstraints::defaultSamplePeriodNs;
    std::uint64_t maxIntraPulseGapSamples = 1;
    std::size_t minSamplesPerPulse = 1;
    bool uniqueFrequencies = true;
};
```

Добавить:

```cpp
enum class PulseGroupingMode
{
    GapThreshold,
    AdaptiveGap,
};

struct SignalParameterEstimatorConfig
{
    std::uint64_t samplePeriodNs = core::DomainConstraints::defaultSamplePeriodNs;

    std::uint64_t maxIntraPulseGapSamples = 1;
    std::size_t minSamplesPerPulse = 1;
    bool uniqueFrequencies = true;

    PulseGroupingMode groupingMode = PulseGroupingMode::AdaptiveGap;

    // Нижняя граница большого разрыва между импульсами.
    // Если 0, estimator подбирает порог автоматически.
    std::uint64_t minInterPulseGapSamples = 0;

    // Защита от слишком агрессивного объединения.
    // Если 0, ограничение не применяется.
    std::uint64_t maxPulseWidthSamples = 0;
};
```

Логика:

* `GapThreshold` должен сохранить старое поведение: импульс продолжается, если `gap <= maxIntraPulseGapSamples`.
* `AdaptiveGap` должен автоматически отделять внутримпульсные разрывы от межимпульсных.

---

# 2. Реализовать adaptive grouping

В `SignalParameterEstimator::buildPulses(...)` сейчас группировка идет по фиксированному gap.

Нужно изменить так:

1. Сначала собрать и отсортировать samples по `bandIndex`.
2. Для каждого band вычислить gaps между соседними `sampleIndex`.
3. Подобрать threshold для группировки.

## Алгоритм подбора threshold

Для каждого band:

```text
gaps = diff(sample[i].sampleIndex - sample[i-1].sampleIndex)
```

Отбросить нулевые gaps, потому что два луча могут иметь один sampleIndex.

Если `groupingMode == GapThreshold`:

* использовать `maxIntraPulseGapSamples`.

Если `groupingMode == AdaptiveGap`:

### Вариант с явным `minInterPulseGapSamples`

Если `minInterPulseGapSamples > 0`:

* разрыв `gap >= minInterPulseGapSamples` считать границей между импульсами;
* остальные gaps считать внутримпульсными.

Тогда фактический threshold:

```cpp
adaptiveThreshold = minInterPulseGapSamples - 1;
```

### Автоматический вариант

Если `minInterPulseGapSamples == 0`:

* отсортировать positive gaps;
* найти самый большой скачок между соседними gap values;
* разделить gaps на “малые” и “большие”;
* threshold взять как максимальный gap из нижнего кластера.

Пример:

```text
gaps: 1, 1, 2, 3, 4, 281250, 281251
threshold должен быть около 4
```

Если данных недостаточно:

* fallback на `maxIntraPulseGapSamples`.

Важно:

* threshold должен быть минимум `maxIntraPulseGapSamples`;
* не допускать threshold = 0;
* не объединять всё в один импульс, если есть явные большие паузы.

---

# 3. Учитывать несколько beamIndex на одном sampleIndex

В текущем estimator два samples с одинаковым `sampleIndex`, но разными `beamIndex`, могут увеличивать `sampleCount`. Это нормально.

Но при расчёте gap нужно использовать переходы между разными sampleIndex:

```cpp
if (current.sampleIndex == previous.sampleIndex) {
    gap = 0;
    // это точно тот же момент времени, не граница импульса
}
```

Для группировки:

* `gap == 0` всегда внутри текущего импульса;
* `gap > 0` сравнивать с threshold.

---

# 4. Исправить расчёт длительности импульса для sparse samples

Сейчас длительность:

```cpp
pulseWidthUs = (lastSampleIndex - firstSampleIndex + 1) * samplePeriodNs / 1000
```

Это правильно, если внутри импульса есть samples, покрывающие почти весь active window.

Но при sparse generation последний sample внутри импульса может быть значительно раньше фактического конца pulse window. Поэтому нужно улучшить генератор, чтобы внутри окна импульса он давал samples ближе к началу и к концу окна.

См. раздел 5.

В estimator оставить формулу `(last - first + 1)`, но добавить защиту:

* если `lastSampleIndex < firstSampleIndex`, не учитывать pulse;
* если `maxPulseWidthSamples > 0` и ширина больше этого значения, такой pulse можно отбросить как некорректно объединённый.

---

# 5. Исправить `SimulatorBcoSampleSource`: генерировать samples по модельному времени, а не только по scene step

Файлы:

```text
src/hardware/simulator/simulator_bco_sample_source.h
src/hardware/simulator/simulator_bco_sample_source.cpp
```

Текущий `generateBatch()` использует:

* `signalStep % scene.sources.size()`
* один source за итерацию
* `nextSampleIndex += sampleIndexStep`

Из-за этого для одного band samples разрежены, и estimator видит микроскопические импульсы.

Нужно изменить генерацию так, чтобы внутри активного pulse window для каждого активного source/band формировались samples на последовательных или почти последовательных sampleIndex.

## Требование к генерации

В одной итерации модельного времени `sampleIndex` нужно рассматривать все источники сцены, а не только один source.

Пример логики:

```cpp
while (batch.samples.size() < samplesPerBatch && attempts < maxAttempts) {
    const auto sampleIndex = nextSampleIndex;

    for each source in scene.sources:
        absoluteFrequencyHz = sourceAbsoluteFrequency(source, signalStep)
        config = findBandContainingFrequency(...)
        pulseConfig = findPulseConfigForBand(...)

        if inside pulse window for this band:
            generate beam samples for this source at this sampleIndex

    ++signalStep;
    nextSampleIndex += sampleIndexStep;
}
```

Так для каждого band/source внутри импульса появятся samples на соседних `sampleIndex`, а estimator сможет восстановить длительность импульса.

Важно:

* Не генерировать больше `samplesPerBatch`.
* Если batch уже заполнен, прервать inner loop.
* `sampleIndex` должен продвигаться даже если все источники в паузе.
* `signalStep` должен продвигаться один раз на модельный sampleIndex, а не на каждый source.
* `sourceAbsoluteFrequency(source, signalStep)` должен по-прежнему работать для frequency drift.

---

# 6. Улучшить промотку пауз в симуляторе

Сейчас `maxAttempts = samplesPerBatch * 20`, а пауза при PRI=100000 мкс и PW=10000 мкс составляет около 281250 sampleIndex. Поэтому генератор может долго не добираться до следующего импульса.

Добавить оптимизацию:

Если для текущего `sampleIndex` ни один source не дал samples, можно перейти к следующему sampleIndex как сейчас.

Но если все bands находятся в паузе, нужно уметь прыгнуть к ближайшему следующему началу импульса.

Добавить helper:

```cpp
std::optional<std::uint64_t> nextPulseStartSampleIndex(
    std::uint64_t currentSampleIndex,
    std::uint64_t samplePeriodNs,
    const std::vector<SimulatorPulseBandConfig>& pulseConfigs);
```

Логика:

* для каждого enabled valid pulse config вычислить следующий `sampleIndex`, где `phaseNs == 0` или начинается новое pulse window;
* вернуть минимальный следующий индекс больше текущего;
* если сейчас какой-то band внутри pulse window, вернуть `currentSampleIndex`.

Использовать осторожно:

* если на текущем `sampleIndex` не было samples и все pulse configs сейчас вне active window, можно прыгнуть на ближайший pulse start;
* не прыгать назад;
* не зацикливаться.

Допустимо сделать более простой вариант:

* увеличить `maxAttempts` так, чтобы покрывать минимум один полный PRI для текущих настроек;
* но предпочтительно реализовать jump, чтобы симулятор не тратил CPU на длинные паузы.

---

# 7. Передать generator settings в estimator через ScanController

Сейчас `ScanController` создает `processing::SignalParameterEstimator m_signalParameterEstimator` с default config.

Нужно настроить estimator так, чтобы он корректно обрабатывал sparse или semi-sparse данные генератора.

Вариант без зависимости `ScanController` от `BandListModel`:

* оставить `AdaptiveGap` по умолчанию.
* Тогда `ScanController` менять не нужно.

Предпочтительный вариант для этой задачи:

* сделать `AdaptiveGap` default mode.
* Не добавлять зависимость `ScanController -> BandListModel`.
* Не передавать generator PRI/PW напрямую в estimator.

Так estimator останется применимым и для реальных данных.

---

# 8. Проверочные критерии расчёта

Добавить тесты, которые проверяют именно сценарий генератора.

## 8.1. Unit-тест estimator для sparse импульсов

Файл:

```text
tests/processing/tst_signal_parameter_estimator.cpp
```

Добавить тест:

```text
adaptive grouping estimates PRI/PW for sparse pulse samples
```

Данные:

* samplePeriodNs = 320
* pulse 1:

  * samples around sampleIndex 0, 5, 10, ..., 31245
* pulse 2:

  * samples around sampleIndex 312500, 312505, ..., 343745
* expected:

  * PRI ≈ 100000 us
  * PW ≈ 10000 us

Допуск:

* PRI tolerance: 100 us
* PW tolerance: 100 us

Почему допуск:

* sparse samples могут не попасть ровно в последний индекс импульса.

---

## 8.2. Unit-тест estimator не должен объединять два импульса

Данные:

* pulse 1: 0..1000 с шагом 5
* pulse 2: 100000..101000 с шагом 5

Ожидание:

* pulseCount = 2
* не один pulse.

---

## 8.3. Unit-тест simulator pulse generation

Файл:

```text
tests/hardware/tst_simulator_bco_sample_source.cpp
```

Добавить тест:

```text
generator produces contiguous samples inside pulse window
```

Настройки:

* pulsePeriodUs = 100000
* pulseWidthUs = 10000
* sampleIndexStep = 1
* samplesPerBatch достаточно большой, например 512 или 1024.

Проверить:

* samples не пустые;
* для каждого sample:

  * phaseNs < widthNs;
* для каждого band, который присутствует в batch:

  * есть хотя бы несколько разных sampleIndex;
  * gaps между соседними sampleIndex внутри ранней части импульса не огромные.

---

## 8.4. Интеграционный тест generator + estimator

Добавить тест в hardware или processing, где:

1. Создать `SimulatorBcoSampleSource`.
2. Настроить pulse configs:

   * PRI = 100000 us
   * PW = 10000 us
3. Сгенерировать достаточное количество samples через несколько callback batches или direct helper, если доступен.
4. Передать samples в `SignalParameterEstimator`.
5. Проверить для хотя бы одного band:

   * `pulseRepetitionPeriodUs` близко к 100000 us;
   * `pulseWidthUs` близко к 10000 us.

Если прямой доступ к `generateBatch()` private:

* тестировать через `start()` и callback;
* собрать batches до тех пор, пока не будет хотя бы 2 pulse windows;
* затем `stop()`.

Допуск:

* PRI ± 1000 us;
* PW ± 1000 us.

---

# 9. Диагностика

Добавить diagnostics в estimator не требуется.

Но в тестах при ошибке полезно выводить:

* pulseCount;
* calculated PRI;
* calculated PW;
* first/last sampleIndex первых нескольких pulses.

Если existing test framework не поддерживает detailed output, можно оставить обычные assertions.

---

# 10. Ограничения

Не делать в этой задаче:

* не подставлять настройки generator PRI/PW напрямую в ResultTableRow;
* не менять ResultTableModel;
* не менять ResultTablePanel.qml;
* не менять BinaryResultTableStorage;
* не менять формат итоговой таблицы;
* не менять BandSettingsDialog.qml;
* не менять BandConfigController;
* не реализовывать UDP;
* не реализовывать high-load pipeline;
* не менять BearingService;
* не менять WaterfallController, кроме случаев, если тесты требуют минимального include/build fix.

---

# 11. Критерии готовности

После выполнения:

* проект собирается;
* существующие тесты проходят;
* новые тесты проходят;
* `SignalParameterEstimator` в режиме `AdaptiveGap` не считает каждый одиночный sample отдельным импульсом;
* `SimulatorBcoSampleSource` генерирует внутри pulse window достаточно плотную последовательность samples;
* для настроек генератора:

  * PRI = 100000 мкс
  * PW = 10000 мкс

расчёт по samples даёт примерно:

* PRI ≈ 100000 мкс

* PW ≈ 10000 мкс

* итоговая таблица после сканирования больше не должна показывать значения уровня:

  * ППИ ≈ 0,943 мкс
  * ДИ ≈ 0,32 мкс

при настройках generator PRI/PW 100000/10000 мкс.
