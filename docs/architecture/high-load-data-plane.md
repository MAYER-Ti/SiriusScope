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

Current experimental mode:

- `ProcessingEngine` defaults to `Sequential` processing, where one worker runs
  Waterfall, Spectrum, Bearing, and SignalParameter aggregation in order.
- `ParallelFanOut` is an opt-in high-load audit mode. It fans each pooled `SignalBlock`
  out to separate stage workers for Waterfall, Spectrum, Bearing, and SignalParameter
  aggregation.
- The fan-out context owns the pooled block handle until every stage completes, so stage
  workers read the same block without copying samples and the block is not returned to
  the pool early.
- Stage queues are bounded and expose queue depth, max depth, capacity, queue wait
  latency, service latency, submit failures, per-stage processed block/sample counts,
  in-flight block, fallback/rejection, and end-to-end fan-out latency metrics.
- `ParallelFanOut` must not become the GUI runtime default until audit results prove the
  behavior is stable enough for production use.
- 90 MB/s target-raw validation remains gated by environment variables. The short audit
  uses `SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST=1`; strict no-drop assertions are enabled by
  `SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS=1`; long soak validation also requires
  `SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST=1`.
- Perf audits report queue stability and latency/backlog budget status. Defaults are
  `SIRIUSSCOPE_MAX_FANOUT_END_TO_END_MS=8000`,
  `SIRIUSSCOPE_MAX_STAGE_QUEUE_WAIT_MS=8000`, and
  `SIRIUSSCOPE_MAX_STAGE_QUEUE_DEPTH_RATIO=0.95`; non-strict audits warn, while strict
  and soak audits fail on enabled budget violations.
- `SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER=1|2|4|8` is an audit-only target-raw sizing
  control. It scales target-raw samples per batch and batch period together, preserving
  raw throughput while reducing source block rate. `SIRIUSSCOPE_RUN_90MBPS_BATCH_SWEEP=1`
  runs a report-oriented comparison across all supported multipliers.
- `SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION=1` runs an audit-only profile selection
  comparison, defaulting to multipliers `4` and `8`. With
  `SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS=1`, the comparison runs every candidate before
  failing, and fails only if no candidate passes no-drop or the selected candidate
  violates a hard latency/backlog budget. The selected multiplier is engineering audit
  guidance only and does not change the normal GUI runtime default.
- `SIRIUSSCOPE_90MBPS_CAPACITY_PROFILE=current|balanced1024|balanced2048` selects an
  audit-only capacity profile for target-raw sustain runs. Capacity profiles only apply
  to `TargetRawThroughput90MBps + FullTargetRawSustain + ParallelFanOut`; they do not
  change GUI runtime defaults.
- `SIRIUSSCOPE_RUN_90MBPS_CAPACITY_SWEEP=1` fixes `m=8` and compares capacity profiles.
  The sweep runs `current` and `balanced1024` by default. `balanced2048` is included only
  with `SIRIUSSCOPE_INCLUDE_2048_CAPACITY_PROFILE=1` because `SignalBlockPool`
  preallocates block storage. If larger capacity only delays saturation and backlog
  continues growing, the next work is service-latency optimization or latency-aware
  policy, not silently making the larger buffer a production default.
- `SIRIUSSCOPE_ENABLE_VISUAL_BACKPRESSURE_POLICY=1` enables audit-only visual-stage
  degradation for `ParallelFanOut`. Spectrum and Waterfall can use
  `SIRIUSSCOPE_VISUAL_STAGE_POLICY=latest-only|drop-oldest`; Bearing stays lossless
  unless `SIRIUSSCOPE_VISUAL_BEARING_BEST_EFFORT=1`; SignalParameter is always
  normalized to `LosslessRequired`. Visual skipped/coalesced/dropped jobs complete their
  fan-out contexts and are reported separately from critical raw/control data loss.
  Strict target-raw runs fail on visual degradation unless
  `SIRIUSSCOPE_ALLOW_VISUAL_DEGRADATION_IN_STRICT=1` is set.

Strict profile selection:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION = "1"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

Soak profile selection:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST = "1"
$env:SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC = "30"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

Capacity sweep:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_CAPACITY_SWEEP = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST = "1"
$env:SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC = "30"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

Visual overload audit:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER = "8"
$env:SIRIUSSCOPE_ENABLE_VISUAL_BACKPRESSURE_POLICY = "1"
$env:SIRIUSSCOPE_VISUAL_STAGE_POLICY = "latest-only"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

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
- Waterfall delivery uses a bounded FIFO `WaterfallRowQueue`, because waterfall is a
  time line and intermediate rows must not be silently replaced by a latest-only value.
- `ProcessingEngine` pushes every produced row into the queue with original `utcNs`,
  `firstSampleIndex`, `lastSampleIndex`, `rowPeriodNs`, and source/view frequency
  metadata. The UI must not synthesize row time on this path.
- `WaterfallController` drains rows at a bounded UI cadence and adapts aggregated rows to
  the existing render buffer/session row format in queue order.
- Queue overload is explicit: dropped rows are counted in metrics and summarized by
  rate-limited diagnostics. There is no silent latest-only row loss.
- Raw `SignalSample` vectors still do not enter Qt, QML, `SignalSampleBus`,
  `BearingFrameBus`, or `ScanController` in the production high-load bootstrap.

`SpectrumSnapshot` and `BearingSnapshot` remain latest-value exchanges. They represent
current summaries where dropping intermediate UI states is acceptable. Waterfall rows are
different: they are timeline data and must be drained or dropped with explicit overload
accounting.

## 7. Spectrum Aggregation

The spectrum path must consume aggregated or downsampled information from the data plane.
It must not copy every high-load block into a GUI-oriented worker.

Target outputs are low-rate spectrum snapshots:

- visible frequency range;
- per-bin amplitude summary;
- band mapping metadata;
- snapshot timestamp and source latency;
- quality/overload flags.

Current v1 implementation:

- `ProcessingEngine` feeds `SpectrumAggregator` with the same `SignalBlock` stream as the
  waterfall path.
- `SpectrumAggregator` maps absolute frequency to fixed render bins, stores beam 0 / beam
  1 peaks, total peak, hit count, and compact per-band summaries.
- Invalid and out-of-range samples are counted in aggregate metrics/diagnostics and are
  not logged per sample.
- `SpectrumSnapshot` is immutable after publication and is exposed through
  `SnapshotExchange<SpectrumSnapshot>`.
- `SpectrumSnapshotAdapter` polls the latest snapshot at a bounded Qt cadence, adapts bins
  to the current `FrequencyViewportModel`, and updates `SpectrumEnvelopeController`.
- `SpectrumEnvelopeWorker` is retained only as legacy/low-volume compatibility code. The
  production high-load bootstrap does not construct it, connect `ingestBatch`, or copy
  high-load blocks into it.

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

Current v1 implementation:

- `ProcessingEngine` feeds `BearingAggregator` beside the waterfall and spectrum
  aggregators.
- `BearingAggregator` groups samples by time window, frequency bin, and band. It stores
  beam 0 / beam 1 peaks independently and creates estimates only when both beams are
  present in the candidate window.
- The v1 bearing estimate uses amplitude interpolation between two beam axes. Equal beam
  amplitudes point near the current antenna azimuth; stronger beam 0 / beam 1 amplitude
  shifts the estimate toward that beam axis.
- The trusted high-load flat-storage path uses block-local candidate accumulation:
  beam peaks are accumulated per block/window and only touched candidates are merged
  into the open bearing window.
- Detailed bearing micro-timing is disabled by default and can be enabled in perf tests
  with `SIRIUSSCOPE_ENABLE_DETAILED_BEARING_TIMING=1`.
- Incomplete candidates, missing beam 0, and missing beam 1 are counters in
  `BearingSnapshot` and pipeline diagnostics. They are not per-candidate warnings.
- `BearingSnapshot` is immutable after publication and is exposed through
  `SnapshotExchange<BearingSnapshot>`.
- `BearingSnapshotAdapter` polls the latest snapshot at a bounded Qt cadence and passes
  low-volume estimates to `ScanController` only while a scan is active.

## 8.1 SignalParameter Critical-Stage Diagnostics

SignalParameter aggregation is a critical data-plane stage. It remains
`LosslessRequired` even when audit-only visual overload policy is enabled, and
SignalParameter jobs are not dropped, coalesced, or treated as best-effort UI work.

Current v1 diagnostics:

- `SignalParameterAggregator` publishes immutable per-band PRI/PW snapshots and keeps
  raw samples out of Qt, QML, `ScanController`, and UI models.
- The trusted fixed-band ingest path records critical counters: input, accepted and
  rejected samples, touched bands, pulse transitions, active pulse updates, completed
  pulses, out-of-order samples, below-threshold fast skips, and trusted/block-local
  fast-path block counts.
- Detailed hot-loop timing for sample loop, band lookup, pulse-state update, and span
  update is disabled by default. Perf audits can enable it with
  `SIRIUSSCOPE_ENABLE_DETAILED_SIGNAL_PARAMETER_TIMING=1`.
- The default pulse amplitude threshold is the minimum valid domain amplitude, so valid
  input samples keep the existing PRI/PW semantics unless a test or future data-plane
  config explicitly raises the threshold.

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

Current v1 scan flow:

- Production bootstrap constructs `ScanController` with `nullptr` raw buses.
- `BearingSnapshotAdapter` is the high-load scan handoff. It skips snapshots while scan
  is inactive and passes only immutable summaries/estimates to
  `ScanController::acceptBearingSnapshotSummary`.
- `SignalSampleBus` and `BearingFrameBus` remain only legacy/low-volume compatibility
  paths for tests and explicitly constructed demo flows. They are not production
  high-load transports.

## 9.1 Antenna-Aware High-Load Simulator

The high-load simulator must preserve the same physical meaning as real BCO beams:
antenna azimuth changes beam visibility and therefore the directional waterfall and
bearing candidates.

Current v1 implementation:

- `HighLoadSimulatorBcoStreamSource` reads current antenna azimuth through the Qt-free
  `IAntennaAzimuthProvider` interface. `SimulatorAntennaState` implements this provider
  in the app bootstrap.
- The source uses the shared `SimulatorRadioScene` model. Each radio source has an
  azimuth, absolute frequency, peak amplitude, and beam sigma.
- Beam axes are `antennaAzimuthDeg - 30` and `antennaAzimuthDeg + 30`.
- Beam amplitude is `peakAmplitude * exp(-0.5 * (delta / sigma)^2)`.
- Samples below the visible/domain amplitude threshold are not emitted.
- When both beams see the same source, the source may emit two samples with the same
  `sampleIndex` and frequency window, one per beam. This is expected and valid.
- `BcoBatchStats` and `SignalBlockMetadata` carry the antenna azimuth used for the block,
  so aggregators can compute direction without consulting Qt or UI state.

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
- produced/queued/drained/dropped waterfall rows;
- waterfall row queue depth and capacity;
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

The current production simulator path uses `RealBcoEquivalent` by default through the
bounded `DataIngestPipeline`. It remains an engineering load profile, not a selectable UI
profile.

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
