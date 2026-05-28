# Pipeline Data Plane

`src/pipeline` is the first non-Qt high-load data plane scaffold. It owns
block-oriented transport, a fixed block pool, bounded queues, basic metrics,
rate-limited diagnostics, and a minimal processing worker.

The current implementation intentionally does not contain production DSP, bearing
aggregation, storage writer, lock-free queues, SIMD, or GUI snapshot publishing.
Those stages must be added as follow-up data-plane components. The temporary copy from
`BcoSampleBlock::samples` into `SignalBlock` is a migration step; the target source path
should fill pooled contiguous blocks directly.
