# Hardware / Ingest Layer

Hardware / ingest layer for SiriusScope.

Responsibilities:

- UDP/TCP clients and protocol parsers;
- BCO control adapter for reception configuration;
- antenna control adapter;
- real hardware, simulator, and replay adapters behind shared interfaces;
- `HighLoadSimulatorBcoStreamSource` and simulator profile integration;
- protocol-version isolation;
- ingest metrics and aggregated diagnostics;
- handoff into the data plane through blocks, memory pool ownership, and bounded queues.

SiriusScope must not implement a direct RPU adapter. Receiver settings that affect the
RPU are sent to the BCO, and the BCO controls the RPU internally.

The ingest layer must not emit raw high-load sample vectors into QML, `QObject`,
`QAbstractListModel`, `WaterfallController`, `SignalSampleBus`, `BearingFrameBus`, or Qt
queued callbacks. It hands off parsed data to the C++ data plane.

Simulator profiles:

- `UiDemo` - safe default for UI development;
- `MediumLoad` - intermediate integration load;
- `RealBcoEquivalent` - approximately real BCO rate, about 1,000,000 sample slots/s with
  10 ms batches;
- `Stress150Percent` - overload/stress profile.
