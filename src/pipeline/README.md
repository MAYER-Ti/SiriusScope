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

Bearing output is published as immutable `BearingSnapshot` objects through the same
latest-value exchange model. `BearingAggregator` groups samples by time window,
frequency bin, and band, pairs beam 0 / beam 1 peaks inside the window, and counts
incomplete candidates instead of producing per-candidate diagnostics. The Qt layer polls
snapshots and passes low-volume summaries to `ScanController` only while a scan is active.

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
