# Статические метрики проекта

Дата снятия метрик: 2026-06-08.

Документ фиксирует статические метрики репозитория SiriusScope для ВКР. Значения
получены из текущего состояния tracked-файлов и не включают локальные build-артефакты.

## Методика подсчета

- Список файлов: `git ls-files`.
- Строки: физические строки файлов через PowerShell `Get-Content`.
- Production-код: tracked-файлы в `src` с расширениями `.cpp`, `.h`, `.qml`, `.frag`,
  `.vert`.
- C++ production-код: tracked-файлы в `src` с расширениями `.cpp`, `.h`.
- Тесты: tracked-файлы `.cpp` в `tests`.
- CMake: `CMakeLists.txt` и `*.cmake`.
- Компонент C++ считается как уникальный basename модуля, объединяющий `.h` и `.cpp`.
- Компонент QML считается как один `.qml` файл.

Физические строки включают пустые строки и комментарии. Это не логические SLOC.

## Общие метрики

| Метрика | Значение |
| --- | ---: |
| Tracked-файлов всего | 301 |
| Production source-файлов в `src` | 207 |
| Строк production-кода в `src` | 37065 |
| C++ production-файлов в `src` | 184 |
| Строк C++ production-кода в `src` | 32736 |
| QML-файлов | 19 |
| Строк QML | 4224 |
| Shader-файлов | 4 |
| Строк shader-кода | 105 |
| Тестовых `.cpp` файлов | 56 |
| Строк тестов в `.cpp` | 23230 |
| Файлов в `tests` всего | 57 |
| Строк в `tests` всего | 24198 |
| CMake-файлов | 2 |
| Строк CMake | 1445 |
| C++ файлов всего, включая тесты | 240 |
| Строк C++ всего, включая тесты | 55966 |
| C++/QML/CMake/shader файлов всего | 265 |
| Строк C++/QML/CMake/shader всего | 61740 |

## Подсистемы `src`

Физическая структура production-кода содержит 7 основных каталогов-подсистем.

| Подсистема | Файлов | Строк | C++ файлов | QML файлов | Shader-файлов |
| --- | ---: | ---: | ---: | ---: | ---: |
| `app` | 87 | 13404 | 85 | 0 | 2 |
| `core` | 9 | 1462 | 9 | 0 | 0 |
| `hardware` | 32 | 3971 | 32 | 0 | 0 |
| `infrastructure` | 14 | 2464 | 14 | 0 | 0 |
| `pipeline` | 34 | 8617 | 34 | 0 | 0 |
| `processing` | 10 | 2863 | 10 | 0 | 0 |
| `ui` | 21 | 4284 | 0 | 19 | 2 |

Архитектурная документация `docs/architecture/layers.md` описывает 8 логических слоев:
Qt adapter / presentation, Application / control, Core / domain, Hardware / ingest,
Pipeline / data plane, DSP / processing, Storage, Infrastructure support.

## Тесты по подсистемам

| Подсистема тестов | `.cpp` файлов | Строк `.cpp` |
| --- | ---: | ---: |
| `tests/app` | 28 | 8551 |
| `tests/core` | 1 | 173 |
| `tests/domain` | 1 | 295 |
| `tests/hardware` | 8 | 2763 |
| `tests/infrastructure` | 4 | 953 |
| `tests/perf` | 1 | 4173 |
| `tests/pipeline` | 9 | 4622 |
| `tests/processing` | 4 | 1700 |

В CMake зарегистрировано 56 тестовых исполняемых targets и 56 `add_test` записей.

## Доменные модели

В `src/core/domain_models.h` объявлено 8 доменных моделей:

| Модель |
| --- |
| `FrequencyRange` |
| `BandConfig` |
| `BeamSample` |
| `SignalSample` |
| `ScanSector` |
| `TimeBase` |
| `BearingResult` |
| `ResultTableRow` |

## Компонентные группы

| Группа | Модулей / компонентов | Файлов | Строк |
| --- | ---: | ---: | ---: |
| Алгоритмическая обработка `src/processing` | 5 | 10 | 2863 |
| Data plane / pipeline `src/pipeline` | 18 | 34 | 8617 |
| Infrastructure storage | 8 | 11 | 2213 |
| Legacy/app storage adapters | 3 | 6 | 678 |
| QML UI-компоненты | 19 | 19 | 4224 |
| Qt UI/control adapters | 18 | 36 | 8078 |
| Диагностика и метрики | 5 | 9 | 2182 |

### Алгоритмическая обработка

Модули: `bearing_service`, `sample_processor`, `signal_parameter_accumulator`,
`signal_parameter_estimator`, `spectrum_envelope_processor`.

### Data Plane / Pipeline

Модули: `bearing_aggregator`, `bearing_snapshot`, `bounded_block_queue`,
`data_ingest_pipeline`, `pipeline_diagnostics`, `pipeline_metrics`, `processing_engine`,
`signal_block`, `signal_block_pool`, `signal_parameter_aggregator`,
`signal_parameter_snapshot`, `snapshot_exchange`, `source_to_pipeline_bridge`,
`spectrum_aggregator`, `spectrum_snapshot`, `waterfall_aggregator`,
`waterfall_row_queue`, `waterfall_snapshot`.

### Хранение

Infrastructure storage: `binary_result_table_storage`, `binary_waterfall_session_storage`,
`result_table_storage`, `result_table_storage_format`, `settings_storage`,
`waterfall_storage`, `waterfall_storage_format`, `waterfall_storage_paths`.

Legacy/app storage adapters: `inmemoryscanacquisitionrecorder`,
`waterfallscanrecordingadapter`, `waterfallstorage`.

### Интерфейс

QML UI-компоненты: `AntennaIndicator`, `ArcBand`, `BandItem`, `BandModel`,
`BandSettingsDialog`, `FooterDataView`, `Indicator`, `Main`, `MenuBarApp`, `Panel`,
`ResultTablePanel`, `ScanProgressIndicator`, `ScanSpeedControl`, `SpectrumView`,
`StatusChip`, `TargetTracker`, `Theme`, `TopToolbar`, `WaterfallView`.

Qt UI/control adapters: `bandconfigcontroller`, `bandlistmodel`, `bearingsnapshotadapter`,
`frequencygridmodel`, `frequencyviewportmodel`, `recordingcontroller`,
`resulttablecontroller`, `resulttablemodel`, `scancontroller`,
`signalparametersnapshotadapter`, `spectrumenvelopecontroller`, `spectrumsnapshotadapter`,
`statusmodel`, `waterfallcontroller`, `waterfallhistorymodel`, `waterfallitem`,
`waterfallrenderbufferadapter`, `waterfallscanrecordingadapter`.

### Диагностика и метрики

Модули: `diagnostic_log_writer`, `diagnostics_sink`, `diagnosticsservice`,
`pipeline_diagnostics`, `pipeline_metrics`.
