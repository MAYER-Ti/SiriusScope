# SiriusScope Data Flow

This document describes the runtime data flow in SiriusScope.

The target architecture is a high-throughput dataflow system. Older MVP/demo paths are
documented only as transition/current implementation notes and must not be used as the
production design for the real BCO stream.

Related documents:

- `docs/architecture/layers.md` - layer responsibilities and dependency rules.
- `docs/architecture/high-load-data-plane.md` - detailed high-load data plane contract.
- `docs/storage/archive-format.md` - current archive layout and target storage rules.

## 1. Data Flow Principles

SiriusScope must follow these principles:

- the BCO stream is high-load data plane traffic;
- Qt/QML/Application code is control plane and presentation;
- raw hardware packets and raw high-load sample vectors do not enter QML;
- `QObject`, `QAbstractListModel`, `QMetaObject::invokeMethod`, and Qt queued signals must
  not transport large raw payloads;
- protocol parsing is isolated in hardware/ingest adapters;
- data plane stages use blocks, memory pool / block pool ownership, bounded queues, and
  explicit backpressure;
- processing, storage, and snapshot generation must not block the GUI thread;
- simulator and real hardware use the same application-level interfaces;
- SiriusScope sends receiver settings to the BCO only, never directly to the RPU;
- domain data preserves original hardware identifiers such as `sampleIndex`;
- diagnostics are aggregated and rate-limited before logs or UI.

## 2. Target High-Load Signal Flow

```text
BCO UDP stream or HighLoadSimulator
    -> RX / ingest thread
    -> ProtocolParserV*
    -> SampleValidator / packet validation
    -> preallocated SignalBlock from block pool / memory pool
    -> bounded ingest queue
    -> ProcessingEngine / DSP thread pool
    -> WaterfallAggregator
    -> SpectrumAggregator
    -> BearingAggregator
    -> optional/future SignalParameterAggregator
    -> StoragePipeline / StorageWriter
    -> GuiSnapshotPublisher
    -> Qt adapter / presentation models
    -> QML views
```

The high-load flow moves blocks and compact aggregation products. It must not move
unbounded `std::vector<SignalSample>` payloads through callbacks or the Qt event loop.

### Stage Responsibilities

`RX / ingest thread`:

- receives packets or simulator batches;
- timestamps source batches;
- avoids long processing work;
- fills or obtains blocks from the block pool;
- reports source-level metrics.

`ProtocolParserV*`:

- parses protocol-specific packet layout;
- validates protocol version;
- converts packet payload into typed block data;
- rejects malformed packets before they enter the data plane.

`Block pool / memory pool`:

- bounds memory use;
- reuses high-load buffers;
- tracks pool usage and exhaustion;
- participates in backpressure.

`Bounded queues`:

- decouple ingest, DSP, storage, and snapshot stages;
- have documented capacity and overflow policy;
- expose queue depth, dropped block count, and max block age.

`ProcessingEngine`:

- consumes `SignalBlock` handles;
- runs validation, aggregation, and DSP work;
- emits compact aggregated products;
- routes data to storage and snapshot stages without GUI-thread copies.

`GuiSnapshotPublisher`:

- publishes immutable/downsampled snapshots at a bounded cadence, typically 20-30 FPS;
- does not publish every raw block;
- coalesces updates when the GUI is slower than the data plane.

## 3. Control Plane Flow

Control-plane flow is low-rate and may use Qt signals/slots:

```text
QML command
    -> application controller
    -> domain validation
    -> hardware/control interface or data plane control interface
    -> status / aggregated diagnostics
    -> presentation model
    -> QML
```

Examples:

- edit `BandItem` settings;
- start/stop recording;
- select simulator profile;
- start/stop sector scanning;
- change waterfall viewport;
- request history range loading.

Control-plane commands may update data plane configuration through explicit thread-safe
configuration snapshots or command queues. They must not pull raw high-load data into
controllers.

## 4. BCO Reception Configuration Flow

```text
Operator edits BandItem settings
    -> BandConfigController / application controller
    -> BandConfig validation
    -> BCO reception configuration snapshot
    -> BcoCommandAdapter
    -> BCO control protocol
    -> BCO applies receiver settings and controls RPU internally
    -> aggregated diagnostics / StatusBar
```

Rules:

- QML must not build protocol payloads.
- SiriusScope must not send commands to the RPU directly.
- The BCO protocol may support up to 8 configured ranges.
- The current SiriusScope workflow sends 5 ranges from the 5 `BandItem` objects.
- Exact BCO control protocol fields remain `TBD` until the protocol document is added.

## 5. Antenna Azimuth Flow

```text
Antenna / rotating device TCP messages
    -> TcpAntennaClient
    -> AntennaProtocolParserV*
    -> AzimuthProvider
    -> application/control state
    -> AntennaIndicator snapshot/model
    -> ProcessingEngine scan-session context when needed
    -> storage metadata when needed
```

The antenna stream is lower-rate than the BCO signal stream, but it still must not be
parsed or controlled directly by QML. Azimuth values used for bearing must be associated
with time or scan-session context outside QML.

## 6. Sector Scanning And Bearing Flow

### Target Flow

```text
Operator selects sector
    -> ScanController
    -> AntennaMotionPlanner / validation
    -> IAntennaControl
    -> active ScanSession descriptor
    -> ProcessingEngine receives scan-session state
    -> BearingAggregator pairs beams by time/frequency/band window
    -> BearingService receives prepared candidates / compact frames
    -> BearingResult and signal parameter summaries
    -> ResultTableController
    -> AntennaIndicator model
    -> StoragePipeline
    -> aggregated diagnostics / metrics
```

### Rules

- `ScanController` coordinates scanning, selected sector, antenna state, and session
  lifecycle.
- `ScanController` must not receive raw high-load sample vectors.
- Processing engine receives active scan-session state and returns scan summaries and
  bearing results.
- `ResultTable` receives domain-level results, not raw stream data.
- Bearing calculation is outside QML.
- Bearing result color must match the related `BandItem` color.
- `BearingService` is a replaceable algorithmic component, but its input is prepared by
  the data plane.

### Beam Pairing

Target bearing candidates are built by window, not necessarily by exact `sampleIndex`:

- time window;
- frequency bucket or frequency range;
- band index;
- beam index set;
- scan-session context;
- quality/diagnostic flags.

Missing beam data must be counted and summarized by window, band, beam, and scan
session. `MissingBeamSample` must not be emitted as a warning for every sample or every
candidate on the high-load path.

### Current/Legacy Note

The current transition path may still use `SampleProcessor`,
`BearingInputFrame`, `BearingFrameBus`, `SignalSampleBus`, and `ScanController` for
low-rate or MVP integration. That path is not the production high-load design and must
not be extended as a raw stream transport.

## 7. Waterfall Flow

### Target Flow

```text
SignalBlock stream
    -> ProcessingEngine
    -> WaterfallAggregator
    -> time-bucket / frequency-bucket rows
    -> render buffer or immutable WaterfallSnapshot
    -> GuiSnapshotPublisher
    -> WaterfallController presentation adapter
    -> WaterfallView
```

### Rules

- Waterfall is time-bucket aggregation, not a per-`sampleIndex` UI feed.
- A row is built from aggregated data for a time window.
- Beam values are aggregated per frequency cell before color mapping.
- QML may render already prepared colors or a compact render buffer.
- QML must not own high-rate aggregation or domain color calculation.
- Snapshot publish cadence is bounded, typically 20-30 FPS.

### Viewport Interaction

```text
Operator zooms/pans SpectrumView
    -> FrequencyViewportModel
    -> control-plane viewport command
    -> snapshot publisher / history reader selects range
    -> WaterfallView redraws from aggregated snapshot
```

Changing the viewport must not:

- clear stored waterfall history;
- route raw stream data through QML;
- force GUI-thread resampling of raw samples;
- block the GUI while reading history.

The current `WaterfallController` implementation can remain as a transition adapter, but
production waterfall aggregation belongs to `WaterfallAggregator` in the data plane.

## 8. Spectrum Flow

```text
SignalBlock stream
    -> ProcessingEngine
    -> SpectrumAggregator
    -> low-rate SpectrumSnapshot
    -> presentation model
    -> SpectrumView
```

The spectrum path must not copy every high-load block into a GUI-oriented worker.
Snapshots contain the visible range, bin summaries, timing, quality state, and overload
flags.

## 9. Signal Parameter Flow

Signal parameters are intentionally outside the current
`BaselineRawThroughput60MBps` production runtime. The flow below describes the optional /
future capability if PRI/PW is reintroduced after a separate perf audit.

```text
SignalBlock stream
    -> ProcessingEngine
    -> SignalParameterAggregator
    -> per-band / per-scan summaries
    -> ResultTableController
    -> StoragePipeline
```

The signal parameter path stores compact summaries and representative values. It must not
retain every raw sample for the lifetime of a scan unless an explicitly bounded storage
or analysis mode is introduced.

## 10. Storage Flow

### Target Write Flow

```text
SignalBlock / aggregated products
    -> storage bounded queue
    -> StorageWriter thread/stage
    -> append-only binary chunks
    -> index records
    -> metadata updates
    -> storage metrics and aggregated diagnostics
```

### Target Read / History Flow

```text
Application startup or history request
    -> archive index lookup
    -> async archive reader
    -> aggregated rows / render-ready chunks / result rows
    -> presentation snapshots/models
    -> WaterfallView / ResultTable
```

Rules:

- raw/near-raw stream must not be written through GUI/controller paths;
- high-volume data is binary, chunked, and append-only;
- JSON/INI/QSettings are for settings and metadata only;
- SQLite may be used for metadata or indexes only if deliberately introduced, not for
  the raw high-rate stream;
- storage has a bounded queue, backpressure policy, and metrics;
- storage failure must produce aggregated diagnostics and explicit degraded state.

## 11. Simulator Flow

```text
Simulator profile
    -> same BCO stream interface as hardware
    -> same antenna source/control interfaces
    -> same data plane and control plane paths
```

Supported simulator profiles:

- `UiDemo` - safe default for UI development.
- `MediumLoad` - intermediate integration load.
- `RealBcoEquivalent` - approximates real BCO throughput, about 1,000,000 sample slots/s
  with 10 ms batches.
- `Stress150Percent` - overload/stress profile.
- `BaselineRawThroughput60MBps` - current fixed production baseline, packet-aligned to
  59.856 MB/s effective raw BCO input.
- `TargetRawThroughput90MBps` - future development/audit target.

`BaselineRawThroughput60MBps` is the ordinary runtime default. `RealBcoEquivalent` and
`TargetRawThroughput90MBps` are engineering/audit profiles, not current production
defaults.

Current bootstrap uses `HighLoadSimulatorBcoStreamSource` through `IBcoStreamSource`.
Legacy simulator sample-source paths may remain covered by tests but are not the target
production architecture.

## 12. Diagnostics Flow

### Target Flow

```text
Subsystem event or metric
    -> local aggregation / counters
    -> rate limiter
    -> DiagnosticsService / metrics sink
    -> StatusModel and technical logs
    -> StatusBar / optional diagnostics view
```

Diagnostics sources:

- UDP receive and packet loss;
- parser errors and unsupported protocol versions;
- invalid samples;
- queue overflow;
- dropped blocks;
- block pool exhaustion;
- RX/DSP/storage latency;
- storage read/write errors;
- corrupted settings or archive files;
- simulator overload;
- GUI snapshot degradation.

Rules:

- per-sample and per-candidate diagnostics are forbidden on the high-load path;
- diagnostics are aggregated by time window, subsystem, band, queue, scan session, or
  error category;
- UI receives concise operator-facing warnings;
- detailed metrics may be logged, but publication must be rate-limited.

## 13. Startup Flow

```text
Application start
    -> load settings
    -> initialize diagnostics and metrics
    -> initialize archive/index access
    -> restore ResultTable rows asynchronously
    -> restore recent Waterfall snapshots/history asynchronously
    -> select hardware/simulator/replay mode
    -> initialize data plane stages
    -> initialize application controllers and QML models
    -> show MainWindow
```

Startup must not perform long blocking archive reads on the GUI thread.

## 14. Shutdown Flow

```text
Application shutdown
    -> stop control-plane sessions
    -> stop hardware/simulator receivers
    -> stop accepting new data plane blocks
    -> drain or discard bounded queues according to policy
    -> flush storage metadata within a bounded timeout
    -> close logs and release block pool resources
```

Shutdown must not corrupt current archive metadata. Partial recordings must remain
diagnosable through metadata and logs.

## 15. Anti-Patterns

Forbidden high-load data flow patterns:

```text
QML reads UDP packets directly.
QML receives raw sample vectors.
QML calculates bearing from raw samples.
QML loops over high-rate sample arrays.
QMetaObject::invokeMethod carries large raw sample payloads.
SignalSampleBus is used for high-load sample delivery.
BearingFrameBus is used for high-load bearing-frame delivery.
WaterfallController owns production high-load aggregation.
ScanController receives raw high-load vectors.
Spectrum path copies every high-load block into a GUI worker.
Storage writer calls QML methods directly.
Raw stream is stored through SQLite/JSON/INI/QSettings.
Per-sample diagnostics are published to UI/log queues.
Viewport change clears Waterfall history.
Archive read blocks UI during scrollback.
```

## 16. Testing Implications

Data flow should be testable at the boundaries:

- parser converts raw packets to blocks;
- invalid samples are rejected or flagged;
- block pool ownership is correct;
- bounded queues apply documented pressure policy;
- `TimeBase` converts sample indices correctly;
- `WaterfallAggregator` builds time-bucket rows;
- `BearingAggregator` pairs beams by window and aggregates missing-beam diagnostics;
- `StoragePipeline` writes and reads chunked append-only data;
- simulator profiles produce expected rates;
- diagnostics are aggregated and rate-limited.

Performance acceptance criteria are listed in `docs/development/build-and-test.md`.

## 17. Open Points

The following details remain `TBD`:

- exact BCO UDP packet layout;
- exact BCO control protocol;
- exact antenna TCP message layout;
- final bearing algorithm;
- exact `SignalBlock` binary layout;
- final storage chunk format for raw/near-raw stream;
- final queue capacities and overload policies;
- final metrics export format.
