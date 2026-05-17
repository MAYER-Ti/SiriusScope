# План дальнейшей разработки SiriusScope

## 1. Назначение документа

Этот документ фиксирует стратегический план дальнейшей разработки SiriusScope и нужен, чтобы разработчики и Codex удерживали единое направление при крупных изменениях.

Roadmap не заменяет техническое задание, scope и архитектурные документы. Он дополняет их как практический план перехода от развитого UI-прототипа к рабочему вертикальному срезу продукта. Подробные требования остаются в `docs/spec/SiriusScope_TZ_v0.1.md`, границы текущей итерации - в `docs/spec/scope.md`, архитектурные правила - в `docs/architecture/layers.md` и `docs/architecture/data-flow.md`.

Документ не привязан к внешним трекерам задач и не является списком мелких задач. Его цель - задавать правильную последовательность развития подсистем и не позволять временным stub-решениям стать боевой архитектурой.

## 2. Текущее состояние проекта

SiriusScope сейчас находится в состоянии развитого UI-прототипа:

- QML-интерфейс уже существует и является полезной основой для проверки компоновки, основных операторских сценариев и визуальных контрактов.
- В проекте есть `SpectrumView`, `BandItem`, `WaterfallView`, `AntennaIndicator`, `ResultTablePanel`, `StatusBar` / `FooterDataView` и связанные UI-элементы.
- Core/domain слой реализован частично: уже есть базовые доменные модели и ограничения, но не все сценарии доведены до полноценных application use-case.
- Processing слой реализован частично через `SampleProcessor`; он должен стать частью реального runtime-потока, а не обслуживать только демонстрационные данные.
- Waterfall пока опирается на синтетический источник и in-memory storage, что полезно для прототипа, но недостаточно для продукта с сохранением истории между запусками.
- `SpectrumControllerStub`, `WaterfallControllerStub`, `AntennaControllerStub` и аналогичные классы являются временными решениями для связывания UI и прототипного поведения.
- Hardware layer и Infrastructure layer в основном находятся в scaffold-состоянии: реальные UDP/TCP-адаптеры, протокольные парсеры, persistent storage, diagnostics и composition root еще должны быть оформлены.
- Полный use-case пеленгования реализован частично: `ScanController` и `BearingService` уже дают первый вертикальный срез до `AntennaIndicator`, но еще отсутствуют `ResultTableModel`, хранение результатов и полная сквозная связь с будущими аппаратными источниками.

Текущее состояние допустимо как промежуточный этап, но дальнейшая разработка должна смещаться от UI-заглушек к явным application/core/processing/infrastructure/hardware границам.

## 3. Главная цель ближайшего этапа

Главная цель ближайшей разработки - получить минимальный рабочий вертикальный срез, в котором данные проходят через те же архитектурные границы, что и будущая аппаратная эксплуатация:

```text
Simulator или hardware source
    -> parser / validation
    -> SampleProcessor
    -> WaterfallView
    -> ScanController
    -> BearingService
    -> AntennaIndicator
    -> ResultTable
    -> persistent storage
```

Сначала нужно добиться работающего сквозного потока, пусть с минимальными алгоритмами и упрощенными протоколами. Оптимизация, финальная визуальная полировка и расширенные пользовательские функции должны идти после того, как данные перестанут обходить application-level interfaces и начнут сохраняться на диск.

Этот вертикальный срез должен подтвердить:

- simulator и real hardware смогут подключаться через одинаковые application-level interfaces;
- QML получает подготовленные модели и команды, а не raw samples, протоколы или storage-форматы;
- `SampleProcessor`, `ScanController`, `BearingService`, storage и diagnostics находятся в правильных слоях;
- перезапуск приложения не уничтожает полезные Waterfall rows и строки итоговой таблицы.

## 4. Принцип разработки

Дальнейшая разработка должна следовать этим правилам:

- Не наращивать боевую функциональность внутри классов с суффиксом `Stub`. Stub-контроллеры можно использовать только как временную прослойку, пока рядом создается правильный application-facing интерфейс или контроллер.
- Не превращать `SpectrumControllerStub`, `WaterfallControllerStub`, `AntennaControllerStub` и похожие классы в permanent architecture. Если появляется реальная логика, ее нужно переносить в application/domain/processing/infrastructure слой по назначению.
- QML не должен становиться владельцем бизнес-логики. Он может отображать данные, вызывать application commands и показывать preview, но не должен владеть расчетом пеленга, парсингом протоколов, storage или high-rate processing.
- UI должен работать через application-facing models/controllers: `BandListModel`, `BandConfigController`, `WaterfallController`, `ScanController`, `ResultTableModel`, `StatusModel` и аналогичные контракты.
- Simulator и real hardware должны использовать одинаковые application-level interfaces, чтобы UI и business logic не знали источник данных.
- Storage, hardware protocols, bearing calculation и тяжелая processing-логика не должны попадать в QML.
- SiriusScope не управляет РПУ напрямую. Настройки диапазонов, dwell time, фильтры, поляризация, аттенюаторы и другие receiver settings должны идти через BCO control interface; БЦО управляет РПУ внутри аппаратного комплекса.
- Любая новая подсистема должна проектироваться так, чтобы ее можно было тестировать без запуска QML.

## 5. Этапы разработки

### Этап 1. Архитектурный каркас и интерфейсы

Сначала нужно оформить явные интерфейсы между application, processing, infrastructure и hardware adapter слоями:

- `IBcoSampleSource`;
- `IAntennaAzimuthSource`;
- `IBcoControl`;
- `IAntennaControl`;
- `IWaterfallStorage`;
- `IResultTableStorage`;
- `ISettingsStorage`;
- `IDiagnosticsSink`.

Также нужен application bootstrap / composition root, который собирает приложение в разных режимах:

- simulator mode для разработки и тестов;
- hardware mode для реальной аппаратуры;
- replay mode позже, после стабилизации live-пути.

Composition root должен быть единственным местом, где выбираются конкретные реализации источников, контроллеров, storage и diagnostics. UI не должен создавать concrete hardware/storage классы.

### Этап 2. BandConfig и настройки диапазонов

Источник истины по `BandItem` нужно перенести из QML в C++ application/core model.

Целевые компоненты:

- `BandListModel`;
- `BandConfigController`;
- доменная модель `BandConfig` с validation rules.

Целевой путь изменения настроек:

```text
BandSettingsDialog
    -> BandConfigController
    -> BandConfig validation
    -> IBcoControl
    -> diagnostics/status
```

`BandItem` должен отображать и редактировать application state, но не формировать BCO protocol payload и не отправлять команды аппаратуре напрямую.

### Этап 3. Симулятор через реальные интерфейсы

Симулятор должен перестать выдавать готовые `WaterfallRow` напрямую в UI-путь. Он должен имитировать аппаратные источники через те же интерфейсы, что и будущая реальная аппаратура.

Нужны:

- `SimulatorBcoSampleSource`;
- `SimulatorAntennaAzimuthSource`;
- `SimulatorBcoControl`;
- `SimulatorAntennaControl`.

Симулятор должен выдавать поток `BeamSample` / `SignalSample` и азимут, принимать конфигурацию диапазонов через `IBcoControl`, а поведение антенны - через `IAntennaControl`. Это позволит использовать simulator path для integration tests и не создавать отдельную UI-only ветку.

### Этап 4. Реальный runtime-поток Waterfall

Нужно связать `SampleProcessor` с `WaterfallView` через application-level контроллер и render buffer:

```text
IBcoSampleSource
    -> SampleProcessor
    -> WaterfallFrame
    -> WaterfallController
    -> render buffer
    -> WaterfallView
```

В этом потоке должно быть явное преобразование:

```text
domain amplitude 1..127
    -> aggregated amplitude
    -> render intensity
    -> RGBA
```

`WaterfallView` может быть оптимизирован позже, но уже на этом этапе данные должны идти из processing слоя, а не из QML-таймера или synthetic UI generator.

### Этап 5. Persistent Waterfall storage

`InMemoryWaterfallStorage` нужно заменить или закрыть production-путем `BinaryWaterfallStorage`.

Минимальная структура записи:

```text
recordings/
    YYYY-MM-DD_HH-MM-SS/
        metadata.json
        waterfall.bin
        waterfall.idx
```

Минимальные требования:

- запись Waterfall rows;
- чтение диапазона по времени или `sampleIndex`;
- восстановление последних строк после перезапуска;
- отсутствие блокировки GUI при записи и чтении;
- устойчивость к отсутствующим, частично записанным или поврежденным файлам;
- диагностика ошибок storage через общий diagnostics path.

Финальный бинарный формат может уточняться отдельно, но storage interface и асинхронная модель должны быть заложены до расширения UI-функций истории.

### Этап 6. DiagnosticsService и настоящий StatusBar

Статические статусы нужно заменить реальными диагностическими событиями.

Целевые компоненты:

- `DiagnosticsService`;
- `StatusModel`;
- `DiagnosticEvent`.

Диагностика должна приходить от:

- BCO connection/source;
- antenna connection/source;
- protocol parsers;
- `SampleProcessor`;
- storage;
- `ScanController`;
- `BearingService`.

`StatusBar` должен отображать application-level состояние и последние важные события, а не читать низкоуровневые ошибки socket/storage напрямую.

### Этап 7. ScanController

Секторное сканирование должно стать application use-case, а не QML-сценарием.

Целевой поток:

```text
User selects sector
    -> ScanController
    -> AntennaMotionPlanner / validation
    -> IAntennaControl
    -> collect samples during scan
    -> BearingService
    -> ResultTable / AntennaIndicator / storage
```

Логика слепой зоны должна находиться в C++ application/domain logic. QML может оставлять только визуальный preview выбранного сектора и прогресса.

`ScanController` должен координировать команды антенны, сбор данных, состояние сканирования, диагностику и передачу входных кадров в `BearingService`.

### Этап 8. BearingService

Расчет пеленга должен быть выделен в отдельный сервис:

- `BearingService`;
- `BearingInputFrame`;
- `BearingResult`.

Current implementation note: `BearingService` lives in the Processing Layer and
calculates an MVP two-beam bearing estimate from `BearingFrameObservation`
values. `ScanController` enriches `BearingInputFrame` with antenna azimuth,
publishes QML-ready bearing marks, and exposes a signal for the future
`ResultTableController`; persistent result-table storage remains a later stage.

На первом этапе алгоритм может быть минимальным или упрощенным, например тестовым алгоритмом для simulator path. Важно, чтобы место алгоритма было архитектурно правильным: вне QML, независимо от visual item state и с возможностью unit/integration testing.

`BearingResult` должен быть доменным/application результатом, из которого затем строятся отметки для `AntennaIndicator` и строки для `ResultTable`.

### Этап 9. ResultTableModel и хранение результатов

Статический `ResultTablePanel.qml` нужно связать с настоящей моделью и storage.

Целевые компоненты:

- `ResultTableModel`;
- `ResultTableController`;
- `BinaryResultTableStorage`.

Целевой поток:

```text
BearingResult
    -> ResultTableRow
    -> ResultTableModel
    -> ResultTableStorage
    -> QML ResultTablePanel
```

Таблица в текущей итерации остается read-only для оператора, но должна получать реальные результаты сканирования и восстанавливаться после перезапуска.

### Этап 10. Hardware adapter layer

После стабилизации simulator path нужно подготовить реальные аппаратные адаптеры:

- `UdpBcoReceiver`;
- `TcpAntennaClient`;
- `BcoProtocolParserV1`;
- `AntennaProtocolParserV1`;
- `BcoCommandAdapter`;
- `AntennaCommandAdapter`.

Даже если точные форматы протоколов остаются `TBD`, интерфейсы и изоляцию протоколов нужно заложить заранее. Новая версия протокола не должна требовать переписывания UI или domain logic.

Unsupported protocol versions, malformed packets и lost connection должны порождать diagnostics, а не crash.

### Этап 11. Асинхронность и потоки

Целевая потоковая схема:

- GUI thread;
- BCO receiver thread;
- antenna thread;
- processing worker;
- storage worker;
- history loading worker.

Тяжелая работа не должна выполняться в GUI thread:

- прием UDP/TCP;
- парсинг пакетов;
- high-rate sample aggregation;
- Waterfall row preparation;
- запись и чтение архивов;
- восстановление истории;
- расчет пеленга;
- длительная обработка ошибок и recovery.

Конкретные primitives могут уточняться при реализации, но границы ответственности и запрет на блокировку UI должны сохраняться.

### Этап 12. Тестирование

Тестирование должно развиваться вместе с переносом логики из QML/stub в C++ слои.

Направления тестирования:

- core tests для доменных моделей, `BandConfig`, `ScanSector`, `TimeBase`, `BearingResult`;
- processing tests для validation, aggregation, Waterfall frame preparation и invalid input handling;
- infrastructure storage tests для read/write, indexes, metadata, corrupted/missing files и restart recovery;
- application controller tests для `BandConfigController`, `ScanController`, `ResultTableController`, diagnostics routing;
- simulator integration tests для проверки simulator через те же interfaces, что и hardware path;
- end-to-end tests для сценариев:
  - simulator -> processing -> waterfall -> storage;
  - scan -> bearing -> result table;
  - restart -> restore waterfall/result table.

Цель тестов - подтверждать архитектурные границы и сквозные сценарии, а не только отдельные helpers.

## 6. Ближайшая MVP-цель

MVP-1 считается достигнутым, когда выполнен минимальный вертикальный срез:

1. Приложение запускается.
2. Данные идут от simulator source через тот же интерфейс, что и будущая аппаратура.
3. `SampleProcessor` формирует `WaterfallFrame`.
4. `WaterfallView` отображает эти данные.
5. Оператор выбирает сектор.
6. `ScanController` запускает симуляцию сканирования.
7. `BearingService` рассчитывает тестовый пеленг.
8. `AntennaIndicator` показывает результат.
9. `ResultTable` показывает строку результата.
10. Waterfall и `ResultTable` сохраняются на диск.
11. После перезапуска данные восстанавливаются.

Этот MVP не обязан иметь финальный алгоритм пеленгации, финальный binary format или максимальную производительность. Он обязан доказать, что основные подсистемы соединены через правильные интерфейсы и что временные UI/stub пути больше не являются основой product flow.

## 7. Что пока не реализовывать

До завершения вертикального среза не нужно заниматься:

- распознаванием типа РТС;
- картой и географическим фоном;
- экспортом во внешние системы;
- поддержкой 8 лучей;
- сложной фильтрацией, сортировкой и аналитикой итоговой таблицы;
- полной пользовательской настройкой layout;
- сложным replay-режимом;
- premature optimization под 90 МБ/с;
- финальной визуальной полировкой Waterfall.

Эти направления могут оставаться архитектурно возможными, но не должны отвлекать от MVP-1 и не должны добавляться через обходные QML/stub решения.

## 8. Рекомендуемый порядок работ

| Шаг | Направление |
|---:|---|
| 1 | Интерфейсы и composition root |
| 2 | `BandConfigController` и C++ Band model |
| 3 | Simulator через реальные interfaces |
| 4 | Runtime Waterfall через `SampleProcessor` |
| 5 | `BinaryWaterfallStorage` |
| 6 | `DiagnosticsService` и `StatusBar` |
| 7 | `ScanController` |
| 8 | `BearingService` |
| 9 | `ResultTableModel` + storage |
| 10 | Hardware adapters |
| 11 | Потоки и нагрузка |
| 12 | Интеграционные тесты |

Этот порядок можно уточнять для конкретных задач, но нельзя пропускать архитектурные interfaces и simulator path ради быстрого расширения stub/UI-поведения.

## 9. Правила для будущих задач Codex

Перед крупной задачей Codex должен:

- читать `AGENTS.md`;
- читать `docs/README.md`;
- если задача затрагивает architecture, читать `docs/architecture/layers.md` и `docs/architecture/data-flow.md`;
- если задача затрагивает требования, читать `docs/spec/scope.md` и `docs/spec/SiriusScope_TZ_v0.1.md`;
- если задача затрагивает roadmap-направления, читать этот документ.

При реализации:

- не превращать stub-классы в permanent architecture;
- не добавлять business logic в QML;
- не создавать отдельный UI-only путь для simulator;
- не обходить application-level interfaces для hardware, storage, scanning и bearing;
- при добавлении новой подсистемы обновлять документацию;
- при добавлении бизнес-логики добавлять или обновлять тесты;
- не менять соседние модули без необходимости;
- явно фиксировать `TBD`, если точные hardware protocol или binary format еще не известны.
