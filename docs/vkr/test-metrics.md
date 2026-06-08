# Результаты тестов производительности и входного потока

Дата фиксации результатов: 2026-06-08.

Проверялись существующие CTest-тесты, относящиеся к high-load data plane,
симулятору входного BCO-потока, pipeline-метрикам, ingest pipeline и bridge между
источником и pipeline. Основной производственный perf-результат ниже - профиль
`BaselineRawThroughput60MBps`, так как он описан в архитектурной документации как
текущий baseline.

## Команды

```powershell
cmake --build build\win-mingw-debug --target tst_high_load_data_plane tst_high_load_simulator_bco_stream_source tst_data_ingest_pipeline tst_source_to_pipeline_bridge tst_pipeline_metrics tst_pipeline
```

Результат: сборка целевых тестов актуальна, `ninja: no work to do`.

```powershell
ctest --test-dir build\win-mingw-debug -R "tst_high_load_data_plane|tst_high_load_simulator_bco_stream_source|tst_data_ingest_pipeline|tst_source_to_pipeline_bridge|tst_pipeline_metrics|tst_pipeline" --output-on-failure --verbose
```

```powershell
$env:SIRIUSSCOPE_RUN_BASELINE_60MBPS_PIPELINE_TEST='1'; ctest --test-dir build\win-mingw-debug -R tst_high_load_data_plane --output-on-failure --verbose
```

## Сводка запусков

| Запуск | Длительность | Пройдено | Упало | Скорость входного потока | Обработано отсчётов | Потери |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Targeted CTest: pipeline/perf/source tests | 10.77 s real | 6 | 0 | см. сценарии ниже | см. сценарии ниже | 0 в high-load сценариях |
| `MediumLoad` smoke внутри `tst_high_load_data_plane` | 3 s scenario | 1 | 0 | 9.49 MB/s pipeline input | 750000 | 0 dropped samples, 0 dropped blocks |
| `TargetRawThroughput90MBps` accounting smoke внутри `tst_high_load_data_plane` | 3 s scenario | 1 | 0 | 90.174 MB/s raw input | 55808 | 0 dropped samples, 0 dropped blocks |
| `BaselineRawThroughput60MBps` audit | 36.12 s real, 30 s scenario | 1 | 0 | 59.868 MB/s raw input | 111397120 | 0 rejected blocks, 0 dropped blocks, 0 queue dropped blocks |

## Детали baseline 60 MB/s

- Профиль: `BaselineRawThroughput60MBps`.
- Режим обработки: `ParallelFanOut`.
- SignalParameter/PRI/PW stage: отключён, что соответствует текущему baseline.
- Ожидаемая скорость raw BCO: 59.856 MB/s.
- Измеренная скорость raw BCO: 59.868 MB/s.
- `inputSamples`: 111397120.
- `processedSamples`: 111397120.
- `processed/input`: 1.000.
- `rejectedBlocks`: 0.
- `droppedBlocks`: 0.
- `queueDroppedBlocks`: 0.
- `blockPoolExhausted`: 0.
- `fanOutAvgMs`: 25.017.
- `fanOutMaxMs`: 130.528.
- `maxQueueRatio`: 0.094.
- `waterfallQmax`: 12.
- `spectrumQmax`: 3.
- `bearingQmax`: 2.
- Итог теста: passed.

Предупреждение baseline-аудита:

```text
WARNING: source missed batch deadlines: 961
```

Предупреждение не привело к потерям: processed/input остался 1.000, dropped/rejected
счётчики равны нулю, тест завершился успешно.

## Детали targeted CTest

В targeted CTest запуск вошли 6 тестов:

- `tst_pipeline` - passed, 2.75 s.
- `tst_pipeline_metrics` - passed, 0.08 s.
- `tst_data_ingest_pipeline` - passed, 0.46 s.
- `tst_source_to_pipeline_bridge` - passed, 0.44 s.
- `tst_high_load_data_plane` - passed, 6.53 s.
- `tst_high_load_simulator_bco_stream_source` - passed, 0.46 s.

Итог: 100% tests passed, 0 tests failed out of 6.

## Не запускавшиеся gated-аудиты

Следующие режимы в `tst_high_load_data_plane` существуют, но не запускались в этой
фиксации результатов:

- 90 MB/s full/strict/soak pipeline audits.
- 90 MB/s batch sweep.
- 90 MB/s profile selection.
- 90 MB/s capacity sweep.
- SignalParameter ablation.
- Stress-аудит.

Причина: это отдельные audit/soak/stress режимы за переменными окружения
`SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST`,
`SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS`, `SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST`,
`SIRIUSSCOPE_RUN_90MBPS_BATCH_SWEEP`,
`SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION`,
`SIRIUSSCOPE_RUN_90MBPS_CAPACITY_SWEEP`,
`SIRIUSSCOPE_RUN_90MBPS_SIGNAL_PARAMETER_ABLATION` и
`SIRIUSSCOPE_RUN_STRESS_TESTS`. Текущий production baseline проверен отдельным
запуском `SIRIUSSCOPE_RUN_BASELINE_60MBPS_PIPELINE_TEST=1`.
