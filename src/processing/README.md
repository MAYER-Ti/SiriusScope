# DSP / Processing Layer

Processing layer for SiriusScope.

Responsibilities:

- consume data plane blocks from bounded queues;
- validate samples through core/domain rules;
- reject invalid input diagnostically without per-sample warning spam;
- aggregate accepted samples by time window, band, beam, and frequency bucket;
- prepare UI-independent waterfall rows and render snapshots;
- prepare spectrum snapshots;
- pair bearing candidates by time/frequency/band window;
- aggregate signal parameters for scan/result-table output;
- expose throughput and latency metrics.

Dependency direction:

```text
Processing -> Core
```

This layer must not depend on QML, Qt Quick, UI components, hardware adapters, protocol
parsers, storage implementations, `QObject` presentation wrappers, or
`QAbstractListModel`.

`SampleProcessor` and any MVP-era per-`sampleIndex` path must be treated as transition
implementation when used with high-load sources. Target production flow goes through
`ProcessingEngine`, `WaterfallAggregator`, `SpectrumAggregator`, and `BearingAggregator`.
