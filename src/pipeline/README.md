# Pipeline Data Plane

`src/pipeline` is the first non-Qt high-load data plane scaffold. It owns
block-oriented transport, a fixed block pool, bounded queues, basic metrics,
rate-limited diagnostics, a minimal processing worker, v1 waterfall time-bucket
aggregation, v1 spectrum snapshot aggregation, and v1 bearing aggregation.

Waterfall output is delivered through a bounded `WaterfallRowQueue`, not a latest-only
exchange. `ProcessingEngine` pushes every produced time-bucket row with its original
UTC/sample-index timing and frequency metadata; the Qt layer drains rows at a bounded UI
cadence and adapts them to the render buffer. If the UI falls behind, row loss is an
explicit overload event counted by queue metrics and rate-limited diagnostics. Raw
`SignalSample` vectors are not transported through Qt, QML, `SignalSampleBus`, or
`BearingFrameBus` on the high-load path.

Spectrum output is published as immutable `SpectrumSnapshot` objects through the same
latest-value exchange model. `SpectrumAggregator` builds fixed frequency bins and compact
per-band summaries from `SignalBlock` data. The Qt layer adapts the latest snapshot to
`SpectrumEnvelopeController`; `SpectrumEnvelopeWorker` remains legacy compatibility code
and is not production high-load transport.
For high-load monotonic streams, `SpectrumAggregator` uses an incremental window-index
path, so assigning samples to spectrum windows does not require per-sample 128-bit
arithmetic when the sample period does not divide the snapshot period exactly. Exact
128-bit calculation and the older divisible-period fast path remain available for tests,
fallbacks, and non-monotonic inputs.

Bearing output is published as immutable `BearingSnapshot` objects through the same
latest-value exchange model. `BearingAggregator` groups samples by time window,
frequency bin, and band, pairs beam 0 / beam 1 peaks inside the window, and counts
incomplete candidates instead of producing per-candidate diagnostics. The Qt layer polls
snapshots and passes low-volume summaries to `ScanController` only while a scan is active.
In the trusted high-load flat-storage path, bearing aggregation accumulates candidate
peaks in block-local buffers and merges only touched candidates into the open bearing
window. Detailed bearing micro-timing is disabled by default to keep `Clock::now()` calls
out of the per-sample loop; perf tests can enable it with:

```powershell
$env:SIRIUSSCOPE_ENABLE_DETAILED_BEARING_TIMING = "1"
```

Signal parameter output is published as immutable `SignalParameterSnapshot` objects.
`SignalParameterAggregator` reuses the processing accumulator inside the data plane and
publishes compact per-band PRI/PW summaries without returning raw samples to Qt. Signal
parameter snapshots are throttled in the data plane, and processing flush can force a
final snapshot so scan completion receives complete PRI/PW summaries. The default
data-plane policy publishes signal parameter snapshots by processed-block interval, not
on every processed block. The default high-load signal parameter path uses a trusted
fixed-band batch ingest loop, so accumulator updates and per-band sample span tracking
share one tight pass over accepted samples while validated, sorted, and map-backed safe
modes remain available for untrusted inputs.

`ProcessingEngine` supports an experimental `ParallelFanOut` mode for high-load audits.
In this mode a popped `SignalBlock` is fanned out to separate Waterfall, Spectrum,
Bearing, and SignalParameter stage workers. The pooled block handle is owned by a shared
fan-out context and returns to the pool only after all four stages complete. Sequential
processing remains the default runtime mode; perf tests can opt into fan-out with:

Parallel fan-out mode reports per-stage backlog diagnostics: queue depth, max depth,
capacity, queue wait latency, service latency, submit failures, and processed
blocks/samples. These metrics are used to identify whether high-load saturation is
caused by one slow stage or by accumulated queueing delay.

```powershell
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
```

90 MB/s strict and soak validation is gated through perf-test environment variables so
ordinary `ctest` runs stay short. A short strict no-drop audit can be run with:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

The active duration defaults to 10 seconds and can be changed with:

```powershell
$env:SIRIUSSCOPE_90MBPS_DURATION_SEC = "30"
```

Long soak validation is also opt-in:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST = "1"
$env:SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC = "60"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

Latency and backlog budgets are configurable:

```powershell
$env:SIRIUSSCOPE_MAX_FANOUT_END_TO_END_MS = "8000"
$env:SIRIUSSCOPE_MAX_STAGE_QUEUE_WAIT_MS = "8000"
$env:SIRIUSSCOPE_MAX_STAGE_QUEUE_DEPTH_RATIO = "0.95"
```

In non-strict target-raw audits, budget violations are printed as report-only warnings.
In strict and soak audits, enabled budget violations fail the perf test.

The target-raw simulator can coalesce batches while preserving the same raw throughput.
This is an audit-only sizing experiment: the multiplier increases samples per source
batch and the batch period together, so the raw byte target stays near 90 MB/s while the
block rate decreases. A single multiplier audit can be run with:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER = "4"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

To compare the supported multipliers `1`, `2`, `4`, and `8`, run the report-oriented
sweep:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_BATCH_SWEEP = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

The sweep prints raw throughput, blocks per second, samples per block, queue backlog, and
per-stage service latency. It is not a replacement for strict no-drop validation.

To compare the current high-load batch candidates and select an audit recommendation,
run profile selection. This mode compares `m=4` and `m=8` by default, prints no-drop,
latency, queue, service, and block-pool summaries, and applies strict failures only after
all candidates have run:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION = "1"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

Soak profile selection remains opt-in and uses the soak duration:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST = "1"
$env:SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC = "30"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

`SIRIUSSCOPE_90MBPS_PROFILE_SELECTION_MULTIPLIERS=4,8` can override the candidate
list with supported multipliers. The selected multiplier is audit guidance only; it does
not change the ordinary runtime GUI default.

Capacity profiles are a separate audit-only sizing control for
`TargetRawThroughput90MBps + m=8 + ParallelFanOut`. A single target-raw audit can select
one profile with:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
$env:SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER = "8"
$env:SIRIUSSCOPE_90MBPS_CAPACITY_PROFILE = "balanced1024"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

The capacity sweep fixes `m=8`, compares `current` and `balanced1024` by default, prints
capacity values, estimated pool memory, queue ratios, backlog trend classification, and
a selected capacity profile or `none`:

```powershell
$env:SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST = "1"
$env:SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_CAPACITY_SWEEP = "1"
$env:SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST = "1"
$env:SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC = "30"
$env:SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS = "1"
ctest --test-dir build/win-mingw-debug -R tst_high_load_data_plane --output-on-failure
```

`SIRIUSSCOPE_INCLUDE_2048_CAPACITY_PROFILE=1` adds `balanced2048`. This is opt-in because
`SignalBlockPool` preallocates block storage. Larger capacity does not become a GUI
runtime default and is not considered a production fix if the backlog trend remains
growing or saturating.

`SourceToPipelineBridge` decouples `IBcoStreamSource` callbacks from
`DataIngestPipeline::ingestSamples()`. The source callback only submits immutable
`BcoSampleBlock` pointers into a bounded RX queue; a dedicated bridge worker performs the
pipeline ingest call and reports received/enqueued/dropped/ingested/rejected block
metrics. Runtime `WaterfallController` uses `SourceToPipelineBridge` so source callbacks
are decoupled from pipeline ingest in both perf tests and normal recording.

`HighLoadSimulatorBcoStreamSource` is antenna-aware: it reads the current azimuth through
a Qt-free provider interface, evaluates the shared simulator radio scene against two beam
axes, and emits beam samples only when the source is visible to that beam. Duplicate
`sampleIndex` values across beam 0 / beam 1 are expected.

The current implementation intentionally does not contain production DSP,
`StoragePipeline`, lock-free queues, SIMD, or direct source fill into pooled buffers.
Those stages must be added as follow-up data-plane components. The temporary copy from
`BcoSampleBlock::samples` into `SignalBlock` is a migration step; the target source path
should fill pooled contiguous blocks directly.
