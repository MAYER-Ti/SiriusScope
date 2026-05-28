# Archive Format

This document describes the current persistent storage format and the target storage
architecture constraints for SiriusScope.

The current format stores aggregated waterfall rows and final result table rows. It is
not a complete production raw/near-raw BCO stream storage format.

For high-load data plane rules, see `docs/architecture/high-load-data-plane.md`.

## 1. Storage Architecture Rules

High-volume stream data must not be written through the GUI/controller path.

Target storage pipeline:

```text
SignalBlock / aggregated products
    -> bounded storage queue
    -> StorageWriter thread/stage
    -> binary chunk files
    -> index files
    -> metadata files
    -> aggregated diagnostics and metrics
```

Rules:

- storage writer is asynchronous and separate from QML/application controllers;
- high-volume data is binary, chunked, append-only, and indexed;
- storage has a bounded queue, backpressure policy, and metrics;
- storage overload is diagnosed and reported in aggregated/rate-limited form;
- JSON/INI/QSettings are acceptable for settings and metadata only;
- SQLite may be used for metadata or indexes only if deliberately introduced;
- SQLite/JSON/INI/QSettings are not valid raw high-rate stream stores;
- file I/O must not block the GUI thread.

Required storage metrics:

- storage queue depth;
- written MB/s;
- write latency;
- dropped or skipped chunks;
- max block/chunk age;
- metadata flush latency;
- storage error counters.

## 2. Current Waterfall Storage Layout

Waterfall sessions are stored under the configured data root:

```text
SiriusScopeData/
    recordings/
        YYYY-MM-DD_HH-mm-ss_<sessionId>/
            metadata.json
            waterfall.bin
            waterfall.idx
```

One directory is one Waterfall recording session.

This current format stores aggregated waterfall rows. It is compatible with the target
architecture as an aggregated storage product, but it is not the raw BCO stream hot path.

## 3. Current Result Table Storage Layout

Final result table rows are stored under the same configured data root:

```text
SiriusScopeData/
    result_table/
        metadata.json
        result_table.bin
        result_table.idx
```

The result table is a cross-session final scan result table in the current iteration. It
is not stored inside a specific Waterfall session directory because `ResultTableRow` does
not currently carry a session identifier.

## 4. `metadata.json`

`metadata.json` is a human-readable session descriptor. Current format version `1`
stores:

- `id`;
- `startUtcMs`;
- `endUtcMs`;
- `rowPeriodMs`;
- `binCount`;
- `bandCount`;
- `beamCount`;
- `sourceName`;
- `closed`;
- `waterfallBinFile`;
- `waterfallIndexFile`;
- `schema`;
- `byteOrder`;
- `rowRecordVersion`.

The current schema name is `WaterfallSessionStorage`; byte order is `little-endian`.

Metadata is low-volume control/storage information. It must not be used to carry raw
high-load samples.

## 5. `waterfall.bin`

`waterfall.bin` starts with `WaterfallBinFileHeader`:

```text
magic            "SSWFALL\0"
formatVersion    1
headerSize       sizeof(WaterfallBinFileHeader)
binRecordSize    sizeof(WaterfallBeamBinDisk)
reserved0        0
```

It is followed by sequential row records. Each row has:

```text
recordMagic       "WFRO" as little-endian uint32
recordVersion     1
utcMs
firstSampleIndex
lastSampleIndex
viewMinHz
viewMaxHz
binCount
payloadSizeBytes
crc32             reserved, currently 0
payload           binCount * WaterfallBeamBinDisk
```

`WaterfallBeamBinDisk` stores two `uint16_t` values: left beam amplitude and right beam
amplitude.

High-load target note: these rows should be produced by `WaterfallAggregator` from
time-bucket aggregation, not by sending every raw `sampleIndex` through the UI path.

## 6. `waterfall.idx`

`waterfall.idx` starts with `WaterfallIndexFileHeader`:

```text
magic            "SSWIDX\0\0"
formatVersion    1
headerSize
recordSize
reserved0        0
```

Each index record stores:

```text
utcMs
firstSampleIndex
lastSampleIndex
fileOffset
rowByteSize
binCount
```

Records are appended in arrival order and are expected to be time-sorted for normal live
recording. If the index is missing or invalid at startup, SiriusScope rebuilds it by
scanning `waterfall.bin`.

## 7. Result Table Storage

### `result_table/metadata.json`

`metadata.json` is a human-readable descriptor for the result table store. Format
version `2` stores:

- `schema`;
- `formatVersion`;
- `byteOrder`;
- `resultTableBinFile`;
- `resultTableIndexFile`;
- `rowCount`;
- `updatedUtcMs`.

The current schema name is `ResultTableStorage`; byte order is `little-endian`.

### `result_table.bin`

`result_table.bin` starts with `ResultTableBinFileHeader`:

```text
magic            "SSRTBIN\0"
formatVersion    2
headerSize       sizeof(ResultTableBinFileHeader)
byteOrder        0x04030201
reserved0        0
```

It is followed by sequential length-prefixed row records. Each record has:

```text
recordMagic       "SSTR" on little-endian disk
recordVersion     2
payloadSizeBytes
crc32             reserved, currently 0
payload
```

The payload stores:

```text
sampleIndex
resultTimeUtcNs
bearingAzimuthDeg
antennaAzimuthDeg
bandIndex
qualityState
qualityValue
frequencyCount
frequenciesHz[]
diagnosticCount
diagnostics[]     ValidationCode + message bytes
```

Version 2 stores both azimuth values. `bearingAzimuthDeg` is the calculated bearing shown
in the result table and must match the bearing rendered by `AntennaIndicator`.
`antennaAzimuthDeg` is retained as scan context for storage/internal diagnostics.

Result table records are domain-level results. They must not contain raw high-load sample
vectors.

### `result_table.idx`

`result_table.idx` starts with `ResultTableIndexFileHeader`:

```text
magic            "SSRTIDX\0"
formatVersion    2
headerSize
recordSize
reserved0        0
```

Each index record stores:

```text
resultTimeUtcNs
sampleIndex
fileOffset
recordByteSize
bandIndex
```

The v2 reader can restore rows by scanning `result_table.bin`; the index is written for
future range loading and fast history lookup.

## 8. Reliability Rules

- Missing or corrupted session metadata makes only that session unavailable.
- Missing `waterfall.bin` skips the session.
- Missing or corrupted `waterfall.idx` is recoverable when `waterfall.bin` can be
  scanned.
- Missing `result_table.bin` means the table starts empty.
- Invalid or unsupported result-table headers make only result-table history unavailable.
- Partial or corrupted records are diagnosed and reading stops at the damaged record.
- `crc32` is reserved in current formats and written as `0`; mandatory CRC validation is
  a future extension.
- Storage diagnostics are reported through the common diagnostics path in aggregated,
  rate-limited form.

## 9. Target Raw/Near-Raw Stream Storage

The final raw/near-raw BCO stream storage format is `TBD`.

When introduced, it must:

- be binary, chunked, append-only, and versioned;
- store blocks or chunks directly from the data plane, not from GUI/controller paths;
- include timing, source, band, beam, and sequence metadata needed for replay and
  diagnostics;
- include indexes for time/frequency/session lookup;
- support recovery after partial writes;
- expose storage queue depth, write throughput, write latency, dropped chunks, and error
  counters;
- define an explicit backpressure policy for overload.
