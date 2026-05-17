# Archive Format

This document describes the current persistent storage format for WaterfallView history
and final result table rows.

## Layout

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

Final result table rows are stored under the same configured data root:

```text
SiriusScopeData/
    result_table/
        metadata.json
        result_table.bin
        result_table.idx
```

The result table is a cross-session final scan result table in the current
iteration. It is not stored inside a specific Waterfall session directory because
`ResultTableRow` does not currently carry a session identifier.

## `metadata.json`

`metadata.json` is a human-readable session descriptor. Format version `1` stores:

- `id`
- `startUtcMs`
- `endUtcMs`
- `rowPeriodMs`
- `binCount`
- `bandCount`
- `beamCount`
- `sourceName`
- `closed`
- `waterfallBinFile`
- `waterfallIndexFile`
- `schema`
- `byteOrder`
- `rowRecordVersion`

The current schema name is `WaterfallSessionStorage`; byte order is `little-endian`.

## `waterfall.bin`

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

`WaterfallBeamBinDisk` stores two `uint16_t` values: left beam amplitude and right beam amplitude.

## `waterfall.idx`

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

Records are appended in arrival order and are expected to be time-sorted for normal live recording. If the index is missing or invalid at startup, SiriusScope rebuilds it by scanning `waterfall.bin`.

## Reliability Rules

- Missing or corrupted session metadata makes only that session unavailable.
- Missing `waterfall.bin` skips the session.
- Missing or corrupted `waterfall.idx` is recoverable when `waterfall.bin` can be scanned.
- Storage diagnostics are reported through the common diagnostics sink.
- `crc32` is reserved in v1 and written as `0`; mandatory CRC validation is a future extension.

## Result Table Storage

### `result_table/metadata.json`

`metadata.json` is a human-readable descriptor for the result table store.
Format version `1` stores:

- `schema`
- `formatVersion`
- `byteOrder`
- `resultTableBinFile`
- `resultTableIndexFile`
- `rowCount`
- `updatedUtcMs`

The current schema name is `ResultTableStorage`; byte order is `little-endian`.

### `result_table.bin`

`result_table.bin` starts with `ResultTableBinFileHeader`:

```text
magic            "SSRTBIN\0"
formatVersion    1
headerSize       sizeof(ResultTableBinFileHeader)
byteOrder        0x04030201
reserved0        0
```

It is followed by sequential length-prefixed row records. Each record has:

```text
recordMagic       "SSTR" on little-endian disk
recordVersion     1
payloadSizeBytes
crc32             reserved, currently 0
payload
```

The payload stores:

```text
sampleIndex
resultTimeUtcNs
antennaAzimuthDeg
bandIndex
qualityState
qualityValue
frequencyCount
frequenciesHz[]
diagnosticCount
diagnostics[]     ValidationCode + message bytes
```

### `result_table.idx`

`result_table.idx` starts with `ResultTableIndexFileHeader`:

```text
magic            "SSRTIDX\0"
formatVersion    1
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

The v1 reader can restore rows by scanning `result_table.bin`; the index is
written for future range loading and fast history lookup.

### Result Table Reliability Rules

- Missing `result_table.bin` means the table starts empty.
- Invalid or unsupported result-table headers make only result-table history unavailable.
- Partial or corrupted records are diagnosed and reading stops at the damaged record.
- Result-table storage diagnostics are reported through the common diagnostics sink.
- `crc32` is reserved in v1 and written as `0`; mandatory CRC validation is a future extension.
