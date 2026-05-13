# Processing layer

Processing layer for SiriusScope.

Responsibilities:
- accept Core/Domain `SignalSample` values;
- validate samples through Core/Domain rules;
- reject invalid input diagnostically;
- aggregate accepted samples by band, beam, frequency bin, and `sampleIndex`;
- prepare UI-independent Waterfall rows;
- prepare intermediate two-beam bearing input frames.

Dependency direction:

```text
Processing -> Core
```

This layer must not depend on QML, Qt Quick, UI components, hardware adapters, protocol parsers, or storage implementations.
