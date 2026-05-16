# Waterfall Archive Format

This document describes the current persistent storage format for WaterfallView history.

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
