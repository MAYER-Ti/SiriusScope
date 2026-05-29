# Pipeline Data Plane

`src/pipeline` is the first non-Qt high-load data plane scaffold. It owns
block-oriented transport, a fixed block pool, bounded queues, basic metrics,
rate-limited diagnostics, a minimal processing worker, and v1 waterfall
time-bucket aggregation.

Waterfall output is published as immutable `WaterfallSnapshot` objects through
`SnapshotExchange`. The GUI polls the latest snapshot and adapts aggregated rows to its
render buffer; raw `SignalSample` vectors are not transported through Qt, QML,
`SignalSampleBus`, or `BearingFrameBus` on the high-load path.

The current implementation intentionally does not contain production DSP,
`SpectrumAggregator`, `BearingAggregator`, `StoragePipeline`, lock-free queues, SIMD, or
direct source fill into pooled buffers. Those stages must be added as follow-up
data-plane components. The temporary copy from `BcoSampleBlock::samples` into
`SignalBlock` is a migration step; the target source path should fill pooled contiguous
blocks directly.
