# Core / Domain Layer

Core/domain layer for SiriusScope.

Responsibilities:

- signal, time, band, scan, bearing, and storage metadata domain models;
- validation rules independent from UI;
- `TimeBase` and preservation of original BCO `sampleIndex`;
- domain-level diagnostics categories and quality states;
- layer-stable contracts shared by data plane, control plane, storage, and hardware
  adapters.

Core/domain must not depend on QML, Qt Quick, concrete hardware adapters, concrete
storage writers, or presentation models.

High-load note:

- domain types may describe `SignalBlock` metadata and aggregation results;
- raw high-load transport ownership belongs to the pipeline/data plane layer;
- target bearing pairing is by time/frequency/band window, not exact `sampleIndex`
  equality only.
