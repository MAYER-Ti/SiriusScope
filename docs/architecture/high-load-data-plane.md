# High-Load Data Plane Architecture

This document defines the target high-throughput runtime architecture for SiriusScope.
It supersedes earlier MVP/demo flow descriptions wherever they imply that Qt/QML,
`WaterfallController`, `SignalSampleBus`, `BearingFrameBus`, or any `QObject` callback
path can carry the high-load raw BCO stream.

## 1. Context

The current runtime can use `HighLoadSimulatorBcoStreamSource` with the
`RealBcoEquivalent` profile. That profile is intended to approximate the real BCO data
rate and produces about 1,000,000 sample slots per second with batches every 10 ms.

The older runtime was around 1,280 samples per second. It was useful for proving the UI
layout and early processing path, but it is not the production architecture for a real
BCO stream.

Known transition risks:

- `WaterfallController` currently accumulates incoming blocks up to
  `sourceFlushIntervalMs = 1000`, builds a large `std::vector`, and sends it through
  the older processing path.
- `SampleProcessor` can build waterfall rows per `sampleIndex`, which is not viable at
  high-load rates.
- The current bearing builder may require both beams in one exact candidate; alternating
  beam indices can produce excessive `MissingBeamSample` diagnostics.
- Diagnostics may be published per sample or per candidate, which can saturate the
  diagnostics/log/UI queues.
- `SignalSampleBus` and `BearingFrameBus` pass large `std::vector` payloads through
  callback or Qt queued paths. They are not valid high-load data plane transports.
- `ScanController` and spectrum UI workers must not receive raw high-load sample vectors.

## 2. Plane Separation

SiriusScope is a high-throughput dataflow system with a strict split between data plane
and control plane.

### Data Plane

The data plane owns all high-rate stream work:

- BCO UDP receive or high-load simulator receive;
- packet parsing and validation;
- fixed-size signal blocks;
- preallocated block pool / memory pool;
- bounded queue handoff between stages;
- DSP pipeline execution;
- time-bucket and frequency-bucket aggregation;
- bearing candidate aggregation;
- signal parameter aggregation;
- append-only storage hot path;
- metrics collection for throughput, latency, queue depth, and overload state;
- downsampled immutable snapshot creation for GUI presentation.

The data plane must be implemented in C++ without QML, Qt Quick item state, or
GUI-owned containers in the hot path. Qt Core may be used only where it does not impose
GUI-thread affinity or unbounded queued copies.

### Control Plane

The control plane owns operator-facing orchestration:

- Qt/QML presentation;
- application controllers;
- scan session lifecycle;
- operator commands;
- band and receiver configuration;
- antenna sector commands;
- status and aggregated diagnostics presentation;
- view model updates from immutable snapshots.

Qt signals/slots are acceptable in the control plane and for low-rate status updates.
They must not be used to transport raw high-load samples, large raw sample vectors, or
per-candidate diagnostics.

## 3. Target Runtime Flow

```text
BCO UDP / HighLoadSimulator
    -> RX / ingest thread
    -> protocol parser
    -> preallocated block pool / memory pool
    -> bounded queues
    -> DSP / processing thread pool
    -> waterfall aggregator
    -> bearing aggregator
    -> signal parameter aggregator
    -> storage writer
    -> GUI snapshot publisher
    -> Qt/QML GUI
```

Each arrow crossing a thread boundary must have explicit ownership and backpressure
semantics. Unbounded queues are forbidden in the high-load path.

## 4. Blocks, Memory Pool, And Bounded Queues

The high-load ingest pipeline must move data in blocks, not per sample and not as
freshly allocated vectors.

Target rules:

- A `SignalBlock` represents a bounded batch of parsed samples plus timing and source
  metadata.
- Blocks are allocated from a preallocated block pool / memory pool.
- Ownership is transferred between stages by handles, spans, or move-only descriptors.
- Each stage uses a bounded queue with a documented capacity and overflow policy.
- Backpressure is a normal operating state and must be visible in metrics.
- Queue overflow must be handled explicitly by controlled block drop, input throttling
  where possible, or degraded aggregation fidelity.
- The data plane must never grow memory usage without a configured bound.

## 5. Processing Engine

The target processing engine consumes blocks from bounded queues and produces aggregated
domain outputs:

- waterfall time/frequency buckets;
- beam-paired bearing candidates;
- per-band signal parameters;
- storage chunks;
- GUI snapshots;
- aggregated diagnostics and metrics.

The processing engine must not depend on QML, `QObject` presentation wrappers, or
`QAbstractListModel`. Algorithmic components such as `BearingService` remain replaceable,
but their high-load input must be prepared by data plane aggregators.

## 6. Waterfall Aggregation

Waterfall must not be modeled as a per-`sampleIndex` UI feed.

Target rules:

- Waterfall rows are built from time-bucket aggregation.
- A row represents an aggregation window, not every BCO sample index.
- Frequency cells are produced from aggregated values per band/beam/frequency bucket.
- The GUI receives an immutable render buffer or snapshot at a bounded cadence, typically
  20-30 FPS.
- Viewport changes select and resample already aggregated data. They must not route raw
  stream data through QML or rebuild history from raw samples on the GUI thread.
- Historical loading is asynchronous and returns aggregated rows or render-ready chunks.

The current `WaterfallController` path is a transition/MVP implementation. It can remain
while the high-load data plane is introduced, but it is not the production owner of
high-load processing.

Current v1 implementation:

- `ProcessingEngine` feeds `WaterfallAggregator` with `SignalBlock` data.
- `WaterfallAggregator` builds rows by fixed time buckets (`rowPeriodNs`) and fixed
  frequency bins, with separate beam 0 / beam 1 peak amplitudes.
- `WaterfallSnapshot` is immutable after publication and is handed to the Qt layer by a
  latest-value `SnapshotExchange`.
- `WaterfallController` polls snapshots at a bounded UI cadence and adapts aggregated
  rows to the existing render buffer/session row format.
- Raw `SignalSample` vectors still do not enter Qt, QML, `SignalSampleBus`,
  `BearingFrameBus`, or `ScanController` in the production high-load bootstrap.

## 7. Spectrum Aggregation

The spectrum path must consume aggregated or downsampled information from the data plane.
It must not copy every high-load block into a GUI-oriented worker.

Target outputs are low-rate spectrum snapshots:

- visible frequency range;
- per-bin amplitude summary;
- band mapping metadata;
- snapshot timestamp and source latency;
- quality/overload flags.

## 8. Bearing Aggregation

Bearing candidates must not require exact `sampleIndex` pairing in the target high-load
path.

Target rules:

- Beam pairing is performed by a bearing aggregator using time/frequency/band windows.
- Candidate construction must tolerate alternating beam indices and packet jitter.
- `MissingBeamSample` is not emitted per sample or per candidate on the high-load path.
- Missing-beam diagnostics are aggregated by time window, band, beam, and scan session.
- `BearingService` receives prepared candidate sets or compact frames from the data
  plane, not raw high-load vectors from the Qt event loop.
- Future 8-beam support must remain possible in the aggregation model.

## 9. Scan Architecture

`ScanController` is a control-plane component. It coordinates:

- scan session lifecycle;
- selected sector;
- antenna movement commands;
- active band/receiver configuration;
- scan state exposed to the UI;
- publication of scan summaries and final bearing results.

`ScanController` must not receive raw high-load sample vectors. During an active scan,
the processing engine receives a scan-session descriptor and produces scan summaries,
bearing results, and signal parameter summaries. `ResultTable` receives domain-level
results, not raw stream data.

## 10. Storage Pipeline

Raw or near-raw stream data must not be written through the GUI/controller path.

Target storage rules:

- Storage writer is a dedicated data plane stage or thread.
- High-volume data is binary, chunked, append-only, and indexed.
- SQLite, JSON, INI, or QSettings are acceptable only for metadata, settings, indexes
  where appropriate, and low-volume state. They are not raw high-rate stream stores.
- Storage has a bounded queue, backpressure policy, and metrics.
- Storage errors produce aggregated diagnostics and explicit degradation state.
- Archive readers are asynchronous and return aggregated/history chunks suitable for
  presentation without blocking the GUI thread.

## 11. Diagnostics And Metrics

Diagnostics for operators are aggregated warnings and state changes. Metrics are detailed
pipeline measurements for engineering and field diagnostics.

Per-sample and per-candidate diagnostics are forbidden on the high-load path.

Required pipeline metrics:

- input MB/s;
- processed MB/s;
- dropped blocks;
- queue depth per stage;
- RX latency;
- DSP latency;
- storage latency;
- GUI snapshot FPS;
- max block age;
- block pool usage;
- snapshot publish latency.

Diagnostics and metrics publication must be rate-limited. The UI displays concise
operator-facing state; detailed metrics may go to logs or a diagnostics view when one is
introduced.

## 12. Simulator Profiles

Simulator profiles must be used consistently:

- `UiDemo` - safe default for UI development and visual checks.
- `MediumLoad` - intermediate load for integration work.
- `RealBcoEquivalent` - load approximating real BCO throughput; about 1,000,000 sample
  slots/s with 10 ms batches.
- `Stress150Percent` - overload/stress profile.

`RealBcoEquivalent` must not become the ordinary default until the high-load data plane
is implemented, bounded, and covered by performance tests.

## 13. Acceptance Criteria

High-load readiness is evaluated by profile:

- `UiDemo`: no drops, stable GUI, no warning spam.
- `MediumLoad`: no drops, or only explicitly accepted controlled drops with metrics.
- `RealBcoEquivalent`: no crash, no OOM, bounded queues, responsive GUI, rate-limited
  diagnostics, visible throughput and latency metrics.
- `Stress150Percent`: the system does not die; overload is detected, bounded, and shown
  explicitly.
- Long `RealBcoEquivalent` run: at least 30 minutes without uncontrolled memory growth.

Metrics must show actual throughput and latency. A test that only verifies application
startup under load is not sufficient.

## 14. Migration Plan

Milestone 1: Stabilize high-load runtime.

Milestone 2: Introduce `SignalBlock`, block pool / memory pool, and bounded queues.

Milestone 3: Introduce `ProcessingEngine` v1.

Milestone 4: Introduce `WaterfallAggregator` v1.

Milestone 5: Introduce `SpectrumAggregator` v1.

Milestone 6: Introduce `BearingAggregator` v1.

Milestone 7: Introduce `StoragePipeline`.

Milestone 8: Add end-to-end performance tests for `UiDemo`, `MediumLoad`,
`RealBcoEquivalent`, and `Stress150Percent`.

## 15. Legacy And Transition Paths

The following paths may exist during migration but are not production high-load design:

- `WaterfallController` accumulating raw blocks and sending large vectors through the
  old processing path;
- `SampleProcessor` creating rows for every `sampleIndex`;
- `SignalSampleBus` carrying live raw sample vectors;
- `BearingFrameBus` carrying high-load bearing frames through callback/Qt queued paths;
- per-sample `MissingBeamSample` warnings;
- GUI-oriented spectrum workers copying whole high-load blocks.

Transition code must be documented as legacy/current implementation when mentioned in
docs or comments. New production work must move toward the target flow described above.
