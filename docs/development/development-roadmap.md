# SiriusScope Development Roadmap

This document defines the strategic migration path for SiriusScope.

It no longer describes the target as a minimal MVP/demo vertical slice. The target is a
high-load BCO stream processing product with explicit data plane / control plane
separation.

Authoritative architecture documents:

- `docs/architecture/layers.md`;
- `docs/architecture/data-flow.md`;
- `docs/architecture/high-load-data-plane.md`;
- `docs/architecture/baseline.md`.

## 1. Current State

The repository contains a useful UI and a partial runtime path, but some downstream
components still reflect the earlier demo/MVP-rate architecture.

Current known state:

- QML UI exists and is useful for layout, operator scenarios, and visual contracts.
- Core/domain models and constraints exist partially.
- Processing exists partially through `SampleProcessor`.
- Runtime may use `HighLoadSimulatorBcoStreamSource` and the `RealBcoEquivalent` profile.
- `RealBcoEquivalent` produces about 1,000,000 sample slots/s with 10 ms batches.
- Older downstream paths were designed around much lower rates, around 1,280 samples/s.
- `WaterfallController`, `SignalSampleBus`, `BearingFrameBus`, and `ScanController`
  paths may still carry raw or large vectors in transition scenarios.

This state is useful for discovering pressure points. It is not the target architecture.

## 2. Target Direction

Target runtime:

```text
BCO UDP / HighLoadSimulator
    -> RX / ingest thread
    -> preallocated block pool / memory pool
    -> bounded queues
    -> DSP / processing thread pool
    -> WaterfallAggregator
    -> SpectrumAggregator
    -> BearingAggregator
    -> SignalParameterAggregator
    -> StoragePipeline
    -> GuiSnapshotPublisher
    -> Qt/QML GUI
```

Control plane:

```text
QML
    -> application controllers
    -> validated commands and configuration
    -> scan session lifecycle
    -> status / aggregated diagnostics
    -> immutable snapshots and domain-level results
```

The target design must prove:

- real hardware, simulator, and replay can share the same interfaces;
- QML receives snapshots and command/status models, not raw samples;
- data plane stages use bounded queues and explicit backpressure;
- storage is asynchronous and append-only for high-volume data;
- diagnostics are aggregated and rate-limited;
- performance metrics show actual throughput and latency.

## 3. Development Principles

- Do not move production logic into QML.
- Do not extend stub/demo paths into the high-load data plane.
- Do not use Qt queued paths for raw high-load vectors.
- Keep `WaterfallController` as a presentation adapter in the target design.
- Keep `ScanController` as a control-plane scan lifecycle coordinator.
- Keep `BearingService` replaceable, with input prepared by `BearingAggregator`.
- Keep simulator and real hardware behind shared interfaces.
- Add tests for nontrivial domain, parsing, queue, storage, aggregation, and performance
  behavior.

## 4. Migration Milestones

### Milestone 1. Stabilize High-Load Runtime

Goal: make the current high-load simulator path diagnosable and bounded enough for
development.

Tasks:

- document `UiDemo`, `MediumLoad`, `RealBcoEquivalent`, and `Stress150Percent` profiles;
- prevent `RealBcoEquivalent` from being treated as a safe ordinary default;
- add or expose basic throughput and drop metrics;
- aggregate diagnostics instead of publishing per-sample/per-candidate warnings;
- identify and isolate paths that copy large vectors into GUI/control-plane components.

Acceptance:

- `UiDemo` is safe for UI development;
- `RealBcoEquivalent` overloads are visible and do not crash the app immediately;
- diagnostics/log/UI queues do not get spammed by per-sample messages.

### Milestone 2. `SignalBlock`, Block Pool, And Bounded Queues

Goal: replace raw vector transport with bounded data plane ownership.

Tasks:

- introduce a `SignalBlock` concept with timing/source metadata;
- introduce preallocated block pool / memory pool;
- introduce bounded queues between ingest, processing, storage, and snapshot stages;
- define overflow/backpressure policy;
- add metrics: queue depth, dropped blocks, max block age, block pool usage.

Acceptance:

- high-load data movement has explicit memory bounds;
- unbounded `std::vector` callback/Qt queued transport is removed from the hot path.

### Milestone 3. `ProcessingEngine` v1

Goal: create the central data plane processing engine.

Tasks:

- consume `SignalBlock` handles from bounded queues;
- run validation and dispatch to aggregators;
- keep processing independent from QML/`QObject` presentation;
- publish metrics for processed MB/s and DSP latency.

Acceptance:

- processing can run independently of the GUI thread;
- test coverage exists for basic block processing and overload behavior.

### Milestone 4. `WaterfallAggregator` v1

Goal: replace per-`sampleIndex` waterfall generation with time-bucket aggregation.

Tasks:

- aggregate by time window, frequency bucket, band, and beam;
- produce compact waterfall rows or render buffers;
- publish immutable `WaterfallSnapshot` values at bounded cadence;
- keep viewport changes independent from raw stream replay through QML.

Acceptance:

- waterfall GUI consumes snapshots;
- `WaterfallController` no longer owns production high-load aggregation;
- viewport changes do not clear history or pull raw stream through QML.

### Milestone 5. `SpectrumAggregator` v1

Goal: make spectrum display consume compact snapshots.

Tasks:

- aggregate per visible frequency/bin range;
- avoid copying every high-load block into GUI-oriented workers;
- publish low-rate `SpectrumSnapshot` values.

Acceptance:

- spectrum display remains responsive under high-load profiles;
- no raw high-load vectors are delivered to presentation models.

### Milestone 6. `BearingAggregator` v1

Goal: prepare bearing inputs from high-load blocks without exact `sampleIndex` coupling.

Tasks:

- pair beams by time/frequency/band window;
- aggregate missing-beam diagnostics by window/band/session;
- produce compact candidate sets for `BearingService`;
- keep `ScanController` out of raw sample collection.

Acceptance:

- alternating beam index streams do not create per-sample `MissingBeamSample` spam;
- `BearingService` receives prepared candidates;
- `ResultTable` receives domain-level `BearingResult` values.

### Milestone 7. `StoragePipeline`

Goal: move high-volume persistence to a dedicated append-only pipeline.

Tasks:

- add bounded storage queue;
- write binary chunked append-only files;
- maintain indexes and metadata;
- expose storage throughput/latency/drop metrics;
- define storage backpressure policy.

Acceptance:

- raw/near-raw or aggregated high-volume data is not written through GUI/controller paths;
- history loading is asynchronous;
- storage overload is visible and bounded.

### Milestone 8. End-To-End Performance Tests

Goal: verify the complete data plane under defined simulator profiles.

Tasks:

- add profile-based integration/performance tests;
- measure throughput and latency;
- check bounded memory behavior;
- verify diagnostics rate limiting.

Acceptance:

- `UiDemo`: no drops.
- `MediumLoad`: no drops or accepted controlled drops.
- `RealBcoEquivalent`: no crash, no OOM, bounded queues, responsive GUI, no diagnostics
  spam.
- `Stress150Percent`: system survives by detecting and bounding overload.
- Long `RealBcoEquivalent`: at least 30 minutes without uncontrolled memory growth.

## 5. Current/Legacy Paths To Avoid Extending

These paths may exist during migration but must not become production high-load design:

- `WaterfallController` accumulating high-load raw blocks and forwarding large vectors;
- `SampleProcessor` building waterfall rows per exact `sampleIndex`;
- `SignalSampleBus` carrying high-load raw sample vectors;
- `BearingFrameBus` carrying high-load bearing frames through callback/Qt queued paths;
- `ScanController` collecting raw high-load sample vectors;
- per-sample/per-candidate diagnostics;
- GUI-oriented spectrum path copying every high-load block;
- SQLite/JSON/INI/QSettings used for raw high-rate stream storage.

## 6. Do Not Implement Yet

Until the high-load data plane is stable, do not spend architecture capacity on:

- RTS type recognition;
- map/geographic background;
- external export;
- implemented 8-beam antenna support;
- long-term target tracking;
- advanced result-table tooling;
- final visual polish of WaterfallView that depends on a stable snapshot contract.

## 7. Recommended Work Order

| Step | Direction |
|---:|---|
| 1 | Stabilize high-load runtime and metrics |
| 2 | `SignalBlock`, memory pool, bounded queues |
| 3 | `ProcessingEngine` v1 |
| 4 | `WaterfallAggregator` v1 |
| 5 | `SpectrumAggregator` v1 |
| 6 | `BearingAggregator` v1 |
| 7 | `StoragePipeline` |
| 8 | End-to-end performance tests |

This order can be refined for concrete tasks, but do not skip data plane ownership,
backpressure, and metrics in order to extend old UI/demo paths faster.

## 8. Rules For Future Codex Tasks

Before a large task, Codex must read:

- `AGENTS.md`;
- `docs/README.md`;
- `docs/architecture/layers.md`;
- `docs/architecture/data-flow.md`;
- `docs/architecture/high-load-data-plane.md`;
- `docs/spec/scope.md`;
- this roadmap when the task affects architecture, application flow, storage, simulator,
  hardware adapters, scanning, or bearing.

When implementing:

- do not change unrelated modules;
- do not put business logic in QML;
- do not bypass hardware/simulator abstraction;
- do not use legacy buses as high-load transports;
- update documentation when contracts or behavior change;
- add tests for nontrivial logic and high-load behavior.
