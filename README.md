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

For performance-sensitive work, also follow the high-load acceptance criteria in
`docs/development/build-and-test.md`.
