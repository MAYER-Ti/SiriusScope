#pragma once

#include <cstdint>

namespace siriusscope::infrastructure::storage_format {

inline constexpr char kWaterfallBinMagic[8] = {'S', 'S', 'W', 'F', 'A', 'L', 'L', '\0'};
inline constexpr char kWaterfallIndexMagic[8] = {'S', 'S', 'W', 'I', 'D', 'X', '\0', '\0'};

inline constexpr std::uint32_t kFormatVersion = 1;
inline constexpr std::uint32_t kWaterfallRowMagic = 0x4F524657; // "WFRO" on little-endian disk

#pragma pack(push, 1)

struct WaterfallBinFileHeader
{
    char magic[8];
    std::uint32_t formatVersion;
    std::uint32_t headerSize;
    std::uint32_t binRecordSize;
    std::uint32_t reserved0;
};

struct WaterfallIndexFileHeader
{
    char magic[8];
    std::uint32_t formatVersion;
    std::uint32_t headerSize;
    std::uint32_t recordSize;
    std::uint32_t reserved0;
};

struct WaterfallRowDiskHeader
{
    std::uint32_t recordMagic;
    std::uint32_t recordVersion;
    std::uint64_t utcMs;
    std::uint64_t firstSampleIndex;
    std::uint64_t lastSampleIndex;
    double viewMinHz;
    double viewMaxHz;
    std::uint32_t binCount;
    std::uint32_t payloadSizeBytes;
    std::uint32_t crc32;
};

struct WaterfallBeamBinDisk
{
    std::uint16_t left;
    std::uint16_t right;
};

struct WaterfallIndexRecord
{
    std::uint64_t utcMs;
    std::uint64_t firstSampleIndex;
    std::uint64_t lastSampleIndex;
    std::uint64_t fileOffset;
    std::uint32_t rowByteSize;
    std::uint32_t binCount;
};

#pragma pack(pop)

static_assert(sizeof(WaterfallBeamBinDisk) == 4);

} // namespace siriusscope::infrastructure::storage_format
