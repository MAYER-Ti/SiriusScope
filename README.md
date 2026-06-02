# SiriusScope

SiriusScope is a Qt/C++ desktop system for изделие «Сириус», a radio-technical
reconnaissance system.

The current architecture direction treats SiriusScope as a high-load BCO stream
processing product, not as an MVP/demo GUI. The application must receive a high-speed BCO
stream, process it with predictable latency, calculate bearing, build waterfall and
spectrum summaries, estimate signal parameters, write continuous data, and show
aggregated results to the operator.

Detailed requirements live in `docs/`. Start with `docs/README.md`.

## Product Scope

In scope for the current product direction:

- high-speed BCO stream reception over the hardware/simulator source interface;
- antenna / rotating device azimuth reception;
- shared real hardware, simulator, and replay interfaces;
- sector scanning and scan session lifecycle;
- bearing calculation from aggregated two-beam data;
- waterfall and spectrum visualization from snapshots;
- final read-only `ResultTable`;
- continuous append-only storage for high-volume data and results;
- aggregated diagnostics and pipeline metrics;
- tests for nontrivial domain, processing, storage, simulator, and performance behavior.

Out of scope unless explicitly requested:

- RTS type recognition;
- map background;
- export to external systems;
- advanced result-table editing/filtering/export;
- full user-customizable layout;
- implemented 8-beam antenna support;
- long-term target tracking.

## Architecture

The main architecture rule is data plane / control plane separation.

Data plane:

```text
BCO UDP / HighLoadSimulator
    -> RX / ingest thread
    -> preallocated block pool / memory pool
    -> bounded queues
    -> DSP / processing thread pool
    -> aggregators
    -> storage writer
    -> GUI snapshot publisher
```

Control plane:

```text
Qt/QML GUI
    -> application controllers
    -> configuration, scan commands, status, diagnostics
    -> presentation models and immutable snapshots
```

Key rules:

- QML displays UI and invokes application-level commands only.
- QML must not parse protocols, calculate bearing, write archives, aggregate high-rate
  streams, or receive raw high-load sample vectors.
- `WaterfallController`, `SignalSampleBus`, and `BearingFrameBus` are not valid
  production high-load raw data transports.
- The GUI receives immutable/downsampled snapshots at a bounded cadence, not raw stream
  blocks.
- Storage is an asynchronous append-only pipeline with backpressure and metrics.
- Diagnostics are aggregated and rate-limited.

Authoritative architecture documents:

- `docs/architecture/layers.md`
- `docs/architecture/data-flow.md`
- `docs/architecture/high-load-data-plane.md`
- `docs/architecture/baseline.md`

## Simulator Profiles

Expected simulator profiles:

- `UiDemo` - safe default for UI development.
- `MediumLoad` - intermediate integration load.
- `RealBcoEquivalent` - approximately real BCO rate, about 1,000,000 sample slots/s with
  10 ms batches.
- `Stress150Percent` - overload/stress profile.

`RealBcoEquivalent` must not be treated as an ordinary safe default until the high-load
data plane is implemented, bounded, instrumented, and performance-tested.

## Technology Stack

- C++20
- Boost
- Qt 6 with Qt Quick / QML
- Qt 6.8+ minimum for the current Windows developer workflow
- CMake
- Conan when third-party dependencies are introduced or formalized
- CTest
- Qt Test or Catch2 for unit tests

## Build

Generated build trees must be under `build/`. Do not use the repository-root `build/`
directory itself as a CMake build tree.

Standard local debug build:

```bash
cmake --preset qt-win-mingw-debug
cmake --build build/win-mingw-debug
```

Current Windows MinGW/Ninja executable path:

```text
build/win-mingw-debug/appSiriusScope.exe
```

## Tests

Run CTest from the configured build directory:

```bash
ctest --test-dir build/win-mingw-debug --output-on-failure
```

### Full 90 MB/s Data-Plane Audit

The regular test suite keeps `TargetRawThroughput90MBps` capped as a source-accounting
smoke. To run the uncapped full pipeline audit from PowerShell:

The audit also prints per-aggregator latency breakdown, a spectrum micro-breakdown for
sample loop, window/bin calculation, bin update, band-summary update, close window, and
snapshot build, a bearing micro-breakdown for sample loop, window/bin calculation,
candidate update, close window, snapshot build, and estimate calculation, and a
signal-parameter micro-breakdown for ingest, snapshot decision, finalize, and snapshot
build time.

Spectrum aggregation uses fast window/bin calculations, fixed band-summary storage, and
block-local accumulation in the high-load data plane: bins and band summaries are
accumulated locally per block/window and merged by touched bin/band lists. Detailed
per-sample spectrum timing is disabled by default and can be enabled for diagnostics
with `SIRIUSSCOPE_ENABLE_DETAILED_SPECTRUM_TIMING=1`. Exact fallback calculations and
legacy vector summary storage remain available for compatibility tests.

Signal parameter estimation uses trusted streaming mode with fixed band-state storage
for internally generated high-load samples. The validated sorted mode remains available
for unordered or untrusted inputs and tests. Signal parameter snapshots are not published
on every processed block by default: the data plane uses a processed-block interval
policy and forces a final snapshot during processing flush, so ResultTable receives
complete PRI/PW at scan completion while the high-load path avoids repeated
finalize/snapshot work.
The high-load signal parameter path uses a trusted fixed-band batch ingest loop to avoid
per-sample mode dispatch and per-sample band span updates. Safe validated and sorted
modes remain available for untrusted inputs.
Bearing aggregation uses flat band/bin candidate storage in the high-load data plane to
avoid per-sample map lookups. The map-backed mode remains available for compatibility
tests and sparse or untrusted configurations.

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST
```

To require zero drops and no simulator backpressure:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST
Remove-Item Env:\SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS
```

To audit whether fewer larger source blocks reduce fan-out backlog, keep the 90 MB/s
target and set a batch multiplier. The multiplier scales samples per batch and the batch
period together, so raw throughput remains the same while block rate decreases:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER = "4"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST
Remove-Item Env:\SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE
Remove-Item Env:\SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER
```

The report-oriented sweep compares multipliers `1`, `2`, `4`, and `8`:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_BATCH_SWEEP = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST
Remove-Item Env:\SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_BATCH_SWEEP
```

To compare the current production-audit candidates and select a recommended batch
multiplier for high-load validation, run profile selection:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION = "1"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST
Remove-Item Env:\SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION
Remove-Item Env:\SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS
```

Soak profile selection is also opt-in:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST = "1"
$env:SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC = "30"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST
Remove-Item Env:\SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION
Remove-Item Env:\SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST
Remove-Item Env:\SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC
Remove-Item Env:\SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS
```

`SIRIUSSCOPE_90MBPS_PROFILE_SELECTION_MULTIPLIERS=4,8` can override the candidate
list with supported multipliers. The selected multiplier is audit guidance only and does
not change the ordinary runtime GUI default.

For performance-sensitive work, also follow the high-load acceptance criteria in
`docs/development/build-and-test.md`.
