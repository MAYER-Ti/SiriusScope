# Architecture Baseline

This note describes the current implementation baseline and how it differs from the
target high-load architecture.

For target rules, use:

- `docs/architecture/layers.md`;
- `docs/architecture/data-flow.md`;
- `docs/architecture/high-load-data-plane.md`.

## 1. Current Baseline

`ApplicationBootstrap` is the current composition root. It wires the available UI-facing
models, controllers, simulator/runtime sources, storage placeholders or implementations,
and diagnostics sinks.

The current runtime has already moved beyond the earliest stub-only state. In particular,
the BCO runtime may use `HighLoadSimulatorBcoStreamSource` and the
`RealBcoEquivalent` profile. This exposes high-load pressure that the older downstream
MVP path was not designed to absorb.

## 2. Existing Contracts

Hardware and stream contracts live under `src/hardware/interfaces/` and related
subdirectories. Important contracts include:

- BCO stream source interfaces;
- antenna azimuth source interfaces;
- BCO control interfaces;
- antenna control interfaces.

Infrastructure contracts live under `src/infrastructure/interfaces/` and related
subdirectories. Important contracts include:

- waterfall/result storage interfaces;
- settings interfaces;
- diagnostics sinks.

These contracts must not depend on QML or Qt Quick.

## 3. Transitional Components

The following components may still exist as current implementation or compatibility
pieces:

- `FrequencyViewportModel`;
- `FrequencyGridModel`;
- `SpectrumControllerStub`;
- `SpectrumDecimator`;
- `WaterfallControllerStub`;
- `WaterfallController`;
- `AntennaControllerStub`;
- `SampleProcessor`;
- `SignalSampleBus`;
- `BearingFrameBus`;
- `BearingService`.

They are not all wrong by existence. The rule is narrower:

- they must not become the production transport for the high-load data plane;
- they must not carry raw high-load vectors through Qt queued paths;
- they must not make QML, `QObject`, or presentation models own raw stream processing;
- they must be replaced or constrained as the high-load pipeline is introduced.

## 4. Current Risk Areas

Current/legacy behavior that must be treated as transition work:

- `WaterfallController` can accumulate incoming source blocks up to
  `sourceFlushIntervalMs = 1000`, build a large `std::vector`, and forward it to the old
  processing path.
- `SampleProcessor` can create waterfall rows per `sampleIndex`, which can produce
  millions of `WaterfallCell` values under `RealBcoEquivalent`.
- Bearing candidate construction can require both beams in one exact candidate, causing
  excessive `MissingBeamSample` diagnostics when beam indices alternate.
- Diagnostics can be published too granularly and overload diagnostics/log/UI queues.
- `SignalSampleBus` and `BearingFrameBus` can move large vectors through callback or Qt
  queued paths.
- `ScanController` can be tempted to observe raw samples directly.
- Spectrum update paths can copy every high-load block into GUI-oriented workers.

These are acceptable only as known migration targets, not as production design.

## 5. Target Replacement Direction

The replacement direction is:

```text
BCO UDP / HighLoadSimulator
    -> RX / ingest thread
    -> preallocated block pool / memory pool
    -> bounded queues
    -> ProcessingEngine
    -> WaterfallAggregator
    -> SpectrumAggregator
    -> BearingAggregator
    -> SignalParameterAggregator
    -> StoragePipeline
    -> GuiSnapshotPublisher
    -> Qt/QML presentation
```

`SignalSampleBus` and `BearingFrameBus` may remain for low-rate transition/testing uses
only. They are not valid high-load sample delivery mechanisms.

`WaterfallController` should become a presentation adapter that consumes immutable
waterfall snapshots, not the owner of raw stream aggregation.

`ScanController` should publish scan-session state and consume scan summaries /
`BearingResult` values, not raw sample blocks.

## 6. Simulator Profiles

The baseline recognizes these simulator profiles:

- `UiDemo` - safe default for UI development and visual checks.
- `MediumLoad` - intermediate integration load.
- `RealBcoEquivalent` - approximately real BCO rate; about 1,000,000 sample slots/s with
  10 ms batches.
- `Stress150Percent` - overload/stress profile.

`RealBcoEquivalent` is a high-load verification profile. It must not be treated as a
safe everyday default until bounded queues, block pool ownership, data plane aggregation,
storage backpressure, diagnostics rate limiting, and performance tests are in place.

## 7. Migration Milestones

1. Stabilize high-load runtime.
2. Add `SignalBlock`, block pool / memory pool, and bounded queues.
3. Add `ProcessingEngine` v1.
4. Add `WaterfallAggregator` v1.
5. Add `SpectrumAggregator` v1.
6. Add `BearingAggregator` v1.
7. Add `StoragePipeline`.
8. Add end-to-end performance tests.

## 8. UI Boundary

QML must continue to talk only to application-facing singletons, models, and QML
elements. It must not create hardware adapters, parse packets, read/write raw archives,
inspect low-level diagnostics directly, or receive raw stream vectors.

Future simulator and hardware modes should be selected inside `ApplicationBootstrap` or a
later composition service, not inside QML.
