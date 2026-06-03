# SiriusScope Glossary

This document defines the main project terms and abbreviations used in SiriusScope
documentation, code, issues, and Codex tasks.

Use these terms consistently.

## Product And Project Terms

### SiriusScope

The Qt/C++ desktop software system for изделие «Сириус».

SiriusScope receives high-speed BCO data, runs stream processing and aggregation,
calculates bearing, writes continuous data, and displays aggregated operator-facing
results.

### Изделие «Сириус»

The radio-technical reconnaissance system for which SiriusScope is developed.

### Current Iteration

The current product direction and implementation stage.

It focuses on high-load BCO stream reception, data plane/control plane separation,
waterfall/spectrum snapshots, sector scanning, bearing calculation, result storage,
aggregated diagnostics, metrics, and simulator profiles.

### Current/Legacy Implementation

Existing code or documentation that may still reflect the earlier MVP/demo-rate path.

Examples include raw vector transport through `SignalSampleBus`, `BearingFrameBus`, or
`WaterfallController`. These are transition paths unless explicitly constrained to
low-rate snapshots or tests.

### Future Extension

A feature that may be implemented later but is not part of the current iteration unless
explicitly requested.

Examples:

- RTS type recognition;
- map background;
- external system export;
- 8-beam antenna implementation;
- long-term target tracking;
- advanced result-table filtering and export.

## Radio Domain Terms

### РТР

Радиотехническая разведка. A field concerned with detecting, receiving, analyzing, and
locating radio-technical emissions.

### РЭР

Радиоэлектронная разведка. A broader term related to reconnaissance of electromagnetic
emissions and radio-electronic systems.

### РТС

Радиотехническое средство. A radio-technical system or device that emits signals.

### ИРИ

Источник радиоизлучения. A source of radio emission.

### Signal

A received radio-frequency event or emission represented in SiriusScope at different
levels:

- parsed sample inside a data plane block;
- aggregated time/frequency bucket;
- waterfall cell;
- bearing candidate;
- scan-level result.

### Bearing

Direction to a radio emission source. In Russian project materials: `пеленг`.

Bearing is calculated outside QML from aggregated data prepared by the data plane.

### Azimuth

Current angular position of the antenna or a bearing result angle, usually in degrees.

### Sector

Angular range selected by the operator on `AntennaIndicator` for scanning.

### Blind Zone

Angular range where the antenna center must not be positioned. Antenna control logic must
account for it outside QML.

## Hardware Terms

### АС

Антенная система.

### ПУ

Поворотное устройство.

### СВБ

Project-specific name or abbreviation related to the antenna / rotating device interface.

### РПУ

Радиоприемное устройство.

SiriusScope does not control the RPU directly. The BCO controls the RPU internally, and
SiriusScope sends reception configuration only to the BCO.

### БЦО / BCO

Блок цифровой обработки. Digital processing unit.

In SiriusScope, the BCO accepts reception configuration, controls the RPU internally, and
sends a high-speed signal stream to the software.

### BCO Reception Configuration

Structured receiver settings sent from SiriusScope to the BCO.

It conceptually includes frequency ranges, dwell time, filter parameters, polarization,
attenuators, and other settings needed by the BCO to control reception and the RPU.

The BCO protocol may support up to 8 configured ranges; the current SiriusScope workflow
uses 5.

### Simulator

Software imitation of hardware.

The simulator uses the same interfaces as real hardware and must not require a separate
UI path.

### Simulator Profiles

Named load profiles for the BCO simulator:

- `UiDemo` - safe default for UI development.
- `MediumLoad` - intermediate integration load.
- `RealBcoEquivalent` - approximates real BCO load, about 1,000,000 sample slots/s with
  10 ms batches.
- `Stress150Percent` - overload/stress profile.
- `BaselineRawThroughput60MBps` - current fixed production baseline, packet-aligned to
  59.856 MB/s effective raw BCO input.
- `TargetRawThroughput90MBps` - future development/audit target.

`BaselineRawThroughput60MBps` is the ordinary runtime default. `RealBcoEquivalent` and
`TargetRawThroughput90MBps` are engineering/audit profiles, not current production
defaults.

### Replay

A mode where previously recorded data is used as an input source for testing, debugging,
or analysis.

## Data Plane Terms

### Data Plane

The C++ high-load path that receives, parses, validates, buffers, processes, aggregates,
stores, and snapshots the BCO stream.

The data plane must not depend on QML, Qt Quick item state, or presentation models.

### Control Plane

The Qt/QML/Application path for operator commands, configuration, scan lifecycle,
statuses, aggregated diagnostics, and presentation models.

Qt signals/slots are acceptable in the control plane.

### Ingest Pipeline

The first data plane stages:

```text
BCO UDP / HighLoadSimulator
    -> RX / ingest thread
    -> protocol parser
    -> block pool / memory pool
    -> bounded ingest queue
```

### DSP Pipeline

The data plane stages that transform validated blocks into aggregated outputs:

```text
bounded queues
    -> ProcessingEngine
    -> WaterfallAggregator
    -> SpectrumAggregator
    -> BearingAggregator
    -> SignalParameterAggregator
```

### SignalBlock

A bounded batch of parsed BCO sample data plus timing/source metadata.

Target high-load transport moves `SignalBlock` handles or descriptors, not fresh
unbounded `std::vector<SignalSample>` payloads.

### Memory Pool / Block Pool

Preallocated storage used to reuse `SignalBlock` buffers and bound memory growth.

### Bounded Queue

A queue with explicit capacity and overflow policy. Every high-load thread boundary must
use bounded queues or an equivalent bounded handoff.

### Backpressure

Explicit overload behavior when downstream stages cannot keep up. Backpressure may cause
controlled drops, coalescing, degraded fidelity, or source throttling where available. It
must be visible through metrics and diagnostics.

### Snapshot

Immutable/downsampled data prepared by the data plane for GUI consumption.

Examples:

- `WaterfallSnapshot`;
- `SpectrumSnapshot`;
- scan summary;
- aggregated diagnostics snapshot;
- result-table rows.

Snapshots are low-rate presentation inputs, not raw stream data.

## Data And Protocol Terms

### UDP

User Datagram Protocol. Used by the BCO data stream in the current architecture.

### TCP

Transmission Control Protocol. Used by the antenna / rotating device azimuth stream.

### Protocol Parser

A component that converts raw UDP/TCP packets or messages into typed software structures.

Protocol parsers are isolated from QML and UI code.

### Sample

A discrete received data element from the BCO stream.

Conceptual fields:

- `sampleIndex`;
- `frequencyOffsetHz`;
- `amplitude`;
- `beamIndex`.

High-load processing moves samples in blocks and aggregates, not per-sample UI events.

### `sampleIndex`

The original sample number from the BCO processing session.

SiriusScope must preserve it. It is used by `TimeBase` and storage metadata, but target
bearing candidate pairing is based on time/frequency/band windows, not exact
`sampleIndex` equality only.

### `frequencyOffsetHz`

Frequency offset in hertz relative to the base frequency configured for the related
BCO-controlled reception band.

### `amplitude`

Signal amplitude in conditional units. Valid input range is:

```text
1..127
```

Invalid values must not crash SiriusScope and must be diagnosed in aggregated,
rate-limited form on the high-load path.

### `beamIndex`

Index of the antenna beam associated with a sample.

Current iteration supports:

```text
0
1
```

Future 8-beam support must remain architecturally possible.

### `A0`

Aggregated amplitude for beam 0 in a frequency-time bucket.

### `A1`

Aggregated amplitude for beam 1 in a frequency-time bucket.

### Direction Difference

Normalized difference between two beam amplitudes. A common conceptual form:

```text
D = (A0 - A1) / (A0 + A1)
```

The exact sign convention must be consistent across domain logic, UI colors, and
documentation.

### TimeBase

Domain model that links BCO sample numbering to local and global time.

## UI Terms

### SpectrumView

UI component that displays frequency-domain information and `BandItem` objects.

It consumes low-rate spectrum snapshots. It must not copy every high-load block into GUI
processing.

### BandItem

A frequency-band UI/domain object corresponding to one BCO-controlled reception band.

Current workflow uses exactly 5 `BandItem` objects. `BandItem` must not directly send
hardware commands from QML.

### WaterfallView

Frequency-time visualization component.

Target behavior uses aggregated time/frequency buckets and immutable render snapshots.
Waterfall is not a per-`sampleIndex` UI feed in production design.

### AntennaIndicator

Circular indicator for antenna and bearing visualization.

It displays current antenna azimuth, selected scan sector, scan progress, and bearing
results.

### ResultTable

Read-only final scan result table.

It displays domain-level scan results and loads previous results from storage. It does
not consume raw stream data.

### StatusBar

Bottom status and diagnostics area.

It displays aggregated operator-facing state, not raw per-sample diagnostics.

## Architecture Terms

### Qt Adapter / Presentation Layer

Layer responsible for QML views, `QObject` wrappers, presentation models, and snapshots
safe for QML.

### Application / Control Layer

Layer responsible for orchestration, commands, scan lifecycle, configuration, and status
routing.

### Core / Domain Layer

Layer containing domain models, core rules, and processing-independent concepts.

### Hardware / Ingest Layer

Layer responsible for hardware-facing and simulator-facing receive/control adapters.

### Pipeline / Data Plane Layer

Layer responsible for blocks, memory pool, bounded queues, backpressure, metrics, and
worker-stage ownership.

### DSP / Processing Layer

Layer responsible for signal processing, aggregation, detection-related logic, bearing
candidate preparation, and algorithmic bearing services.

### Storage Layer

Layer responsible for append-only binary data, metadata, indexes, settings, logs, and
history reads.

## Storage Terms

### Binary Storage

File storage for high-volume data such as waterfall chunks, snapshots, or raw/near-raw
stream chunks where enabled.

### Append-Only Storage

Storage model where high-volume records are appended sequentially and indexed. Existing
records are not rewritten in the hot path.

### Metadata Storage

JSON/INI/SQLite/QSettings-compatible storage for low-volume descriptors and settings.
It is not a raw high-rate stream store.

### Technical Log

File-based diagnostic log used for development, debugging, and field diagnostics.

### Rotation

Policy for limiting stored archive files and replacing old data without corrupting the
current recording.

## Development Terms

### MVP

Minimum viable product. In current SiriusScope documentation, MVP usually denotes
legacy/transition implementation, not target architecture.

MVP paths must still respect layer boundaries and must not be extended into high-load
production data plane transports.

### TBD

To be determined. Use `TBD` for unknown protocol details, exact binary formats, or
hardware-specific values that are not finalized yet.

### ADR

Architecture Decision Record.

### Codex Task

A task given to Codex or another AI coding assistant. A good task specifies goal, files
or docs to read, target layer, allowed changes, forbidden changes, and expected tests.
