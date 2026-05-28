# SiriusScope Layered Architecture

This document defines the architectural layers of SiriusScope, their responsibilities,
allowed dependencies, and forbidden dependencies.

The current architecture direction is high-load dataflow with explicit separation between
the C++ data plane and the Qt/QML control plane. See
`docs/architecture/high-load-data-plane.md` for the detailed runtime pipeline.

## 1. Architectural Principle

SiriusScope is not a demo/MVP GUI wrapped around signal samples. It is a
high-throughput desktop system for receiving a BCO stream, processing it with predictable
latency, writing continuous data, and showing aggregated operator-facing state.

Primary rules:

- Qt/QML/Application layer is the control plane and presentation surface.
- High-load stream processing is the C++ data plane.
- Qt signals/slots are acceptable for control-plane commands and low-rate status updates.
- Qt signals/slots, `QObject`, `QAbstractListModel`, and QML must not carry high-load
  raw data, large sample vectors, or per-candidate diagnostics.
- Data plane stages must use explicit ownership, preallocated blocks where practical,
  bounded queues, metrics, and backpressure.

## 2. Logical Layers

SiriusScope is divided into these logical layers:

```text
Qt adapter / presentation layer
Application / control layer
Core / domain layer
Hardware / ingest layer
Pipeline / data plane layer
DSP / processing layer
Storage layer
Infrastructure support layer
```

The physical repository structure may evolve, but code must respect these boundaries.

## 3. Qt Adapter / Presentation Layer

### Responsibilities

The Qt adapter / presentation layer is responsible for:

- QML views and Qt Quick controls;
- `QObject` wrappers and view models;
- low-rate, UI-safe immutable snapshots;
- layout, visual state, and lightweight formatting;
- operator interaction and command invocation;
- displaying aggregated diagnostics and status.

Main UI components include:

- `MainWindow`;
- `SpectrumView`;
- `BandItem`;
- `WaterfallView`;
- `AntennaIndicator`;
- `ResultTable`;
- `StatusBar`;
- configuration dialogs.

### Allowed Logic

QML may:

- render UI;
- bind to prepared models;
- handle simple user interaction;
- call application-level commands;
- display immutable/downsampled snapshots.

### Forbidden Logic

QML must not:

- parse UDP/TCP packets;
- access sockets directly;
- build BCO or antenna protocol payloads;
- calculate bearing;
- run DSP algorithms;
- aggregate high-rate signal streams;
- own long-running timers for data plane processing;
- write raw/near-raw archives;
- read high-volume archive data synchronously;
- receive raw high-load sample vectors.

## 4. Application / Control Layer

### Responsibilities

The application / control layer coordinates operator use cases and control-plane state:

- application bootstrap and composition;
- scan session lifecycle;
- band and receiver configuration orchestration;
- antenna sector commands;
- device mode selection;
- status and diagnostics routing;
- exposing UI-safe state to presentation models;
- connecting GUI commands to domain, hardware, pipeline, and storage interfaces.

Typical components:

- `ApplicationController`;
- `SessionController`;
- `ScanController`;
- `DeviceControlFacade`;
- `ResultTableController`;
- `SettingsService`;
- `FrequencyViewportModel`;
- QML-facing list/table models.

`WaterfallController` can exist as a presentation/controller adapter, but it is not the
production owner of high-load waterfall processing. It consumes snapshots or render-ready
buffers produced by the data plane.

### Scan Rule

`ScanController` is a control-plane component. It coordinates sector scanning, antenna
state, scan session lifecycle, and result publication. It must not receive raw high-load
sample vectors through the Qt event loop.

Target scan flow:

```text
Operator selects sector
    -> ScanController
    -> scan session descriptor
    -> ProcessingEngine / BearingAggregator
    -> scan summaries and BearingResult values
    -> ResultTableController / AntennaIndicator model / storage pipeline
```

### Dependencies

The application / control layer may depend on:

- core/domain models;
- data plane control interfaces;
- processing service interfaces;
- storage interfaces;
- hardware/control interfaces;
- Qt object/model types only for presentation-facing adapters.

It must not implement the high-load data plane itself.

## 5. Core / Domain Layer

### Responsibilities

The core/domain layer contains UI-independent concepts and rules:

- domain models;
- validation rules;
- time base;
- frequency model;
- band configuration;
- scan sector model;
- bearing result model;
- recording metadata;
- diagnostics categories and quality states.

Typical domain models:

- `SignalSample`;
- `BeamSample`;
- `SignalBlock` metadata;
- `BandConfig`;
- `ScanSector`;
- `TimeBase`;
- `WaterfallCell`;
- `WaterfallRow`;
- `BearingCandidate`;
- `BearingResult`;
- `ResultTableRow`;
- `RecordingMetadata`.

### Domain Rules

Domain code defines and enforces rules such as:

- valid input amplitude range is `1..127`;
- input amplitude `0` is invalid;
- original BCO `sampleIndex` must be preserved;
- display time is derived from `TimeBase`, not from UI timers;
- current SiriusScope workflow uses 5 `BandItem` objects;
- current antenna model uses beams `0` and `1`;
- future 8-beam support must remain possible;
- full product frequency range is `0.3..18 GHz`.

The domain layer must not know QML item state, binary protocol layout, or concrete
storage files.

## 6. Hardware / Ingest Layer

### Responsibilities

The hardware / ingest layer hides real hardware and simulator details behind stable
interfaces:

- BCO UDP receive;
- high-load simulator receive;
- antenna TCP receive;
- BCO command formatting and sending;
- antenna command formatting and sending;
- protocol parsing and version handling;
- packet validation before data plane handoff;
- source-level diagnostics and metrics.

Typical components:

- `UdpBcoReceiver`;
- `HighLoadSimulatorBcoStreamSource`;
- `IBcoStreamSource`;
- `TcpAntennaClient`;
- `ProtocolParserV*`;
- `BcoCommandAdapter`;
- `AntennaCommandAdapter`;
- simulator adapters.

The ingest layer outputs parsed blocks or block-fill operations for the pipeline/data
plane layer. It must not emit high-load raw sample vectors to QML or application
controllers.

## 7. Pipeline / Data Plane Layer

### Responsibilities

The pipeline/data plane layer owns the high-load transport and execution model:

- `SignalBlock` ownership;
- preallocated block pool / memory pool;
- bounded queues;
- data plane worker lifecycle;
- backpressure policy;
- drop policy;
- block age tracking;
- throughput and latency metrics;
- fan-out to DSP, storage, and snapshot stages.

Target flow:

```text
BCO UDP / HighLoadSimulator
    -> RX / ingest thread
    -> memory pool block
    -> bounded queues
    -> DSP / processing thread pool
    -> aggregators
    -> storage writer
    -> snapshot publisher
```

The pipeline/data plane layer must not depend on QML, Qt Quick, or presentation models.

## 8. DSP / Processing Layer

### Responsibilities

The DSP/processing layer transforms validated data plane blocks into compact results:

- sample validation and filtering;
- pulse detection where implemented;
- time-bucket aggregation;
- frequency-bucket aggregation;
- waterfall aggregation;
- spectrum aggregation;
- bearing candidate preparation;
- beam pairing by time/frequency/band window;
- signal parameter aggregation;
- algorithmic bearing service execution;
- processing diagnostics and metrics aggregation.

Target components:

- `SampleValidator`;
- `ProcessingEngine`;
- `WaterfallAggregator`;
- `SpectrumAggregator`;
- `BearingAggregator`;
- `SignalParameterAggregator`;
- `BearingService`;
- `SignalDetector` where scope allows it.

### Bearing Rule

`BearingService` remains a replaceable algorithmic component. In the high-load target it
receives prepared candidate sets or compact frames from `BearingAggregator`. It must not
receive raw high-load vectors from `ScanController`, `SignalSampleBus`,
`BearingFrameBus`, or Qt queued callbacks.

### Waterfall Rule

Waterfall is time-bucket aggregation. It must not be described or implemented as a
per-`sampleIndex` UI feed in production design.

## 9. Storage Layer

### Responsibilities

The storage layer owns persistent data:

- append-only binary chunks for high-volume data;
- indexes for range lookup;
- result table storage;
- metadata;
- settings;
- technical logs;
- recovery of partial/corrupted files where possible;
- asynchronous archive reads for history.

Rules:

- raw/near-raw stream data must not be written through GUI/controller paths;
- high-volume stream data must use binary, chunked, append-only formats;
- JSON, INI, SQLite, and QSettings may be used for metadata/settings/low-volume state,
  but not for the raw high-rate stream;
- storage writer must have a bounded queue, backpressure policy, and metrics;
- file I/O must not block the GUI thread.

## 10. Infrastructure Support Layer

Infrastructure support includes technical services that are not themselves the high-load
pipeline:

- settings storage;
- diagnostics sinks;
- logging;
- configuration files;
- archive indexes/cache;
- replay readers;
- utility services.

Infrastructure must not depend on QML UI components or hardware socket implementation
details unless a service is explicitly an adapter.

## 11. Dependency Direction

Preferred dependency direction:

```text
Qt adapter / presentation
    -> Application / control
        -> Core / domain
        -> Pipeline/DataPlane interfaces
        -> DSP/Processing interfaces
        -> Storage interfaces
        -> Hardware/Control interfaces

Hardware / ingest
    -> Core / domain
    -> Pipeline/DataPlane interfaces

Pipeline / data plane
    -> Core / domain
    -> DSP/Processing interfaces
    -> Storage interfaces

DSP / processing
    -> Core / domain

Storage
    -> Core / domain
```

Forbidden dependency examples:

```text
QML -> UDP socket
QML -> protocol parser
QML -> raw stream queue
QML -> archive writer
QML -> DSP algorithm
QML -> bearing algorithm

Application controller -> high-load raw sample vector via Qt queued connection
ScanController -> raw BCO sample stream
WaterfallController -> production high-load aggregation owner
SignalSampleBus -> high-load sample delivery
BearingFrameBus -> high-load bearing-frame delivery

Processing -> QML item
Data plane -> QAbstractListModel
Domain -> Qt Quick
Storage writer -> QML callback
Hardware adapter -> QML component
```

## 12. Threading Expectations

The target runtime must support explicit separation of:

- GUI thread;
- BCO RX / ingest thread;
- antenna control/receive thread;
- DSP / processing thread pool;
- storage writer thread/stage;
- snapshot publisher;
- history loading worker.

Thread boundaries in the high-load path require bounded queues and explicit ownership.
Unbounded queued connections, hidden copies of large vectors, and per-sample dispatch are
forbidden.

## 13. Diagnostics And Metrics

Recoverable errors must be reported without crashing when safe continuation is possible.

Operator diagnostics are aggregated warnings and state changes. Pipeline metrics are
engineering data:

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

Per-sample and per-candidate diagnostics are forbidden in the high-load data plane.
Diagnostics publication must be rate-limited before reaching logs or UI.

## 14. Current Transition Notes

Some current code still reflects the MVP/demo architecture. This is acceptable only as a
temporary implementation state.

Transition paths that must not be treated as production architecture:

- `WaterfallController` accumulating raw blocks and forwarding large vectors;
- `SampleProcessor` building rows per exact `sampleIndex`;
- `SignalSampleBus` or `BearingFrameBus` transporting high-load vectors;
- `ScanController` collecting raw sample vectors;
- per-sample `MissingBeamSample` diagnostics;
- spectrum workers copying every high-load block into GUI-oriented processing.

When these names appear in documentation, they must be described as current/legacy or
transition implementation unless they are explicitly limited to low-rate control-plane
or snapshot delivery.

## 15. Testing Implications

The architecture must make these areas testable without QML:

- domain validation;
- time conversion;
- protocol parsers;
- block pool ownership;
- bounded queue behavior and overload policy;
- processing aggregation;
- waterfall time-bucket aggregation;
- bearing window pairing;
- storage write/read and recovery;
- diagnostics rate limiting;
- simulator profiles and performance acceptance.

Performance acceptance criteria are defined in
`docs/architecture/high-load-data-plane.md` and `docs/development/build-and-test.md`.
