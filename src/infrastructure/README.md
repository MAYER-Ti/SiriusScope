# Infrastructure / Storage Support Layer

Infrastructure support for SiriusScope.

Responsibilities:

- settings;
- metadata;
- technical logging;
- diagnostics sinks;
- archive indexes/cache;
- replay support where applicable;
- support code for append-only binary storage.

High-volume storage belongs to an asynchronous storage pipeline:

```text
data plane products
    -> bounded storage queue
    -> storage writer thread/stage
    -> binary chunked append-only files
    -> indexes and metadata
```

Rules:

- raw/near-raw stream data must not be written through GUI/controller paths;
- JSON/INI/QSettings/SQLite are acceptable for metadata, settings, and indexes where
  appropriate, but not for the raw high-rate stream;
- storage diagnostics are aggregated and rate-limited;
- file I/O must not block the GUI thread;
- storage queue depth, write throughput, write latency, dropped chunks, and error counts
  must be visible as metrics when the high-load pipeline is implemented.
