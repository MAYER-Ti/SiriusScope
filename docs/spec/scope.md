# SiriusScope Scope

This document defines the current product scope of SiriusScope.

It separates:

- general capabilities of изделие «Сириус»;
- the current SiriusScope product direction;
- current/legacy implementation constraints;
- future extensions that must not be implemented unless explicitly requested.

## 1. Product Context

Изделие «Сириус» is a radio-technical reconnaissance system intended for:

- detection and reception of radio-technical signal emissions;
- analysis of received signals;
- measurement of frequency, time, and amplitude parameters;
- bearing calculation for radio emission sources;
- display of received and processed information;
- integration with related systems in future product stages.

SiriusScope is the desktop software component for изделие «Сириус». It replaces a
fragmented workflow of separate legacy applications for hardware control and signal
analysis with one maintainable software system.

## 2. Current Product Direction

SiriusScope must be treated as a high-load BCO stream processing system, not as an
MVP/demo GUI application.

The system must provide:

- high-speed BCO data reception;
- stream-oriented ingest pipeline;
- DSP pipeline and aggregation;
- sector scanning;
- bearing calculation;
- waterfall and spectrum visualization from snapshots;
- signal parameter estimation;
- continuous append-only data storage;
- simulator support through the same interfaces as real hardware;
- aggregated diagnostics and metrics for operator and engineering visibility.

The main functional goal of the current iteration remains:

```text
Bearing calculation and visualization for received signals.
```

RTS type recognition is not part of the current iteration.

## 3. In-Scope Architecture

SiriusScope must use explicit data plane / control plane separation.

Data plane:

- BCO UDP or high-load simulator receive;
- packet parsing and validation;
- `SignalBlock`-style block ownership;
- preallocated block pool / memory pool;
- bounded queue handoff between stages;
- DSP / processing thread pool;
- waterfall, spectrum, bearing, and signal parameter aggregators;
- asynchronous storage writer;
- immutable/downsampled GUI snapshot publisher;
- throughput, latency, queue, drop, and block-age metrics.

Control plane:

- Qt/QML presentation;
- application controllers and commands;
- band and receiver configuration;
- antenna sector commands;
- scan session lifecycle;
- status and aggregated diagnostics;
- QML-facing view models that consume snapshots and domain-level results.

Qt signals/slots may be used for the control plane. They must not be used for raw
high-load sample delivery.

## 4. Technology Stack

Target stack:

- C++20;
- Boost where it provides clear value;
- Qt 6 with Qt Quick / QML;
- CMake;
- Conan when third-party dependencies are introduced or formalized;
- CTest;
- Qt Test or Catch2 for unit tests.

Minimum Qt version for the current Windows developer workflow is Qt 6.8+.

## 5. Hardware And Simulator Integration

SiriusScope must work with:

- real hardware;
- software simulator;
- replay/file-based sources where applicable.

Real hardware and simulator paths use the same application-level and data plane
interfaces. The UI must not know whether data comes from real hardware, simulator, or
replay.

The current simulator profile set is:

- `UiDemo` - safe default for UI development;
- `MediumLoad` - intermediate integration load;
- `RealBcoEquivalent` - approximate real BCO load, about 1,000,000 sample slots/s with
  10 ms batches;
- `Stress150Percent` - overload/stress profile;
- `BaselineRawThroughput60MBps` - current fixed production baseline, packet-aligned to
  59.856 MB/s effective raw BCO input;
- `TargetRawThroughput90MBps` - future development/audit target.

`BaselineRawThroughput60MBps` is the ordinary runtime default. `RealBcoEquivalent` and
`TargetRawThroughput90MBps` are engineering/audit profiles, not current production
defaults.

## 6. Input Data

The current system expects two primary input streams:

1. BCO signal stream over UDP or simulator source.
2. Antenna / rotating device azimuth over TCP or simulator source.

SiriusScope also sends reception configuration to the BCO. The BCO controls the RPU
internally; SiriusScope must not connect to or command the RPU directly.

BCO samples conceptually contain:

- `sampleIndex`;
- `frequencyOffsetHz`;
- `amplitude`;
- `beamIndex`.

The high-load data plane must move these samples in bounded blocks and aggregated
products, not as unbounded per-sample UI events.

## 7. Frequency Model

Full product frequency range:

```text
0.3 GHz to 18 GHz
```

Current SiriusScope workflow:

- 5 `BandItem` objects;
- each `BandItem` represents one BCO band;
- each BCO band is up to 500 MHz wide;
- total simultaneous observation width is up to 2500 MHz.

The BCO control protocol is expected to support up to 8 configured frequency ranges, but
the current UI and domain workflow use 5 ranges. Future support for another number of
bands must remain possible.

## 8. Antenna And Beams

The current iteration assumes a two-beam antenna model:

```text
beamIndex = 0
beamIndex = 1
```

Future support for an 8-beam antenna must remain architecturally possible, but is not
part of the current iteration.

Bearing candidate construction in the target high-load path is based on
time/frequency/band windows. It must not require exact `sampleIndex` pairing.

## 9. Amplitude Constraints

Input amplitude values are expected in the range:

```text
1..127
```

Rules:

- amplitude `0` is invalid for an input sample;
- negative amplitudes are invalid;
- amplitudes above `127` are invalid;
- invalid values must not crash the application;
- invalid values must be rejected, ignored, or marked diagnostically;
- invalid-sample diagnostics must be aggregated and rate-limited on the high-load path.

## 10. Time Model

SiriusScope must preserve the original BCO `sampleIndex`.

The application must provide a time model that supports:

- local time from the start of recording;
- global/system time for display;
- stable reconstruction of historical data after restart;
- synchronization between SpectrumView, WaterfallView, AntennaIndicator, and ResultTable;
- aggregation windows for high-load processing.

The UI must not replace the domain time model with ad hoc visual timestamps.

## 11. SpectrumView And BandItem

`SpectrumView` is responsible for:

- displaying the frequency scale;
- displaying 5 `BandItem` objects;
- changing the visible frequency viewport;
- driving the visible range of `WaterfallView`.

`BandItem` represents and edits settings for one frequency band.

Band settings are passed to application/controller logic first and then to the BCO
control interface. QML must not directly create or send hardware commands.

The spectrum display consumes downsampled `SpectrumSnapshot` data from the data plane. It
must not copy every high-load block into GUI-oriented processing.

## 12. WaterfallView

`WaterfallView` is responsible for frequency-time visualization.

Target requirements:

- display signal history from aggregated time/frequency buckets;
- preserve old rows when viewport changes;
- support future asynchronous loading of old data from files;
- use color based on amplitude and two-beam difference;
- render immutable/downsampled snapshots or render buffers;
- update at a bounded GUI cadence, typically 20-30 FPS.

Waterfall must not be modeled as a per-`sampleIndex` UI feed in production design.
The current `WaterfallController` path is a transition/MVP implementation, not the target
owner of high-load processing.

## 13. AntennaIndicator

`AntennaIndicator` is responsible for:

- displaying current antenna azimuth;
- displaying selected scan sector;
- showing sector scanning progress;
- showing bearing results.

Bearing results are displayed separately per `BandItem`. Bearing result color must match
the related `BandItem` color.

Bearing calculation is performed outside QML. `BearingService` remains replaceable, but
its high-load input must be prepared by the data plane `BearingAggregator`.

## 14. ResultTable

The current iteration uses a final scan result table, not a detailed pulse table.

The result table displays scan-level results such as:

- global result time;
- bearing azimuth;
- antenna azimuth as scan context where needed;
- frequency set / band-related frequencies;
- quality and diagnostic flags.

The table is read-only for the operator in the current iteration.

It receives domain-level results, not raw stream data, and must support loading previously
stored results after startup.

## 15. StatusBar, Diagnostics, And Metrics

`StatusBar` displays current system status and aggregated diagnostics:

- program status;
- BCO connection/status;
- antenna connection/status;
- reception-configuration status;
- current azimuth;
- recording status;
- overload/degraded state;
- latest aggregated warnings.

Pipeline metrics are required for engineering visibility:

- input MB/s;
- processed MB/s;
- dropped blocks;
- queue depth;
- RX latency;
- DSP latency;
- storage latency;
- GUI snapshot FPS;
- max block age;
- block pool usage.

Per-sample and per-candidate diagnostics are forbidden on the high-load path.

## 16. Storage

SiriusScope must preserve useful data between launches.

Current and target storage responsibilities:

- waterfall rows/snapshots or aggregated chunks;
- final result table rows;
- raw/near-raw stream chunks where enabled by product scope;
- metadata;
- settings;
- technical logs;
- indexes/cache for fast history access.

Target storage rules:

- high-volume data is binary, chunked, append-only, and indexed;
- storage writer is an asynchronous data plane stage;
- storage has bounded queues, backpressure policy, and metrics;
- settings may use INI/JSON/QSettings;
- metadata may use JSON/SQLite where appropriate;
- SQLite/JSON/INI/QSettings must not be used for the raw high-rate stream;
- file I/O must not block the GUI thread.

## 17. Testing And Performance

The project must be designed for testability.

Testable areas include:

- domain models;
- time conversion;
- protocol parsers;
- invalid input handling;
- block pool and bounded queue behavior;
- storage read/write and recovery;
- file rotation;
- bearing-related calculations;
- waterfall/spectrum/bearing aggregation;
- diagnostics rate limiting;
- simulator profiles.

Target test coverage:

```text
>= 50%
```

Performance acceptance by simulator profile:

- `UiDemo`: no drops.
- `MediumLoad`: no drops or only explicitly accepted controlled drops.
- `BaselineRawThroughput60MBps`: strict no-drop sustain audit, packet-aligned
  59.856 MB/s effective raw BCO input, Waterfall/Spectrum/Bearing active,
  SignalParameter absent.
- `RealBcoEquivalent`: no crash, no OOM, bounded queues, responsive GUI, rate-limited
  diagnostics, visible throughput and latency metrics.
- `TargetRawThroughput90MBps`: future/audit profile with explicit bottleneck reporting.
- `Stress150Percent`: the system does not die; overload is detected and displayed.
- Long `RealBcoEquivalent` run: at least 30 minutes without uncontrolled memory growth.

## 18. Out-Of-Scope Features

The following features are out of scope unless explicitly requested:

- RTS type recognition or active `SignalClassifier` workflow;
- map background and geospatial target plotting;
- external system export;
- advanced result-table export/filtering/sorting/editing;
- arbitrary user-customizable layout;
- implemented 8-beam antenna support;
- long-term target tracking;
- click-based Waterfall-to-table linking;
- final hardware protocol details before they are provided.

## 19. Current/Legacy Implementation Notes

The existing code may contain useful MVP/demo-era implementation paths. They must be
treated as transition paths:

- `WaterfallController` accumulating source blocks and forwarding large vectors;
- `SampleProcessor` producing rows by exact `sampleIndex`;
- `SignalSampleBus` carrying live raw sample vectors;
- `BearingFrameBus` carrying bearing frames through callback/Qt queued paths;
- per-sample `MissingBeamSample` diagnostics;
- GUI-oriented spectrum workers copying high-load blocks.

These paths can remain temporarily, but target development moves toward
`SignalBlock`/block pool/bounded queues, `ProcessingEngine`, aggregators, storage
pipeline, and GUI snapshots.

## 20. Open Questions

The following items require future clarification:

1. Exact BCO UDP packet format.
2. Exact BCO control protocol.
3. Exact antenna TCP message format.
4. Exact antenna command format.
5. Exact bearing calculation model.
6. Exact grouping model for samples, pulses, and signal batches.
7. Target operating systems.
8. Minimum hardware requirements.
9. Final RGB values for WaterfallView and BandItem colors.
10. Final archive binary formats.
11. Final simulator protocol behavior.
12. Final data plane queue capacities and overload policy.
