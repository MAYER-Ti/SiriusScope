#pragma once

#include <cstdint>

namespace siriusscope::infrastructure::result_table_storage_format {

inline constexpr char kResultTableBinMagic[8] = {'S', 'S', 'R', 'T', 'B', 'I', 'N', '\0'};
inline constexpr char kResultTableIndexMagic[8] = {'S', 'S', 'R', 'T', 'I', 'D', 'X', '\0'};

inline constexpr std::uint32_t kLegacyFormatVersion = 1;
inline constexpr std::uint32_t kFormatVersion = 2;
inline constexpr std::uint32_t kByteOrderLittleEndian = 0x04030201;
inline constexpr std::uint32_t kResultTableRecordMagic = 0x52545353; // "SSTR" on little-endian disk

#pragma pack(push, 1)

struct ResultTableBinFileHeader
{
    char magic[8];
    std::uint32_t formatVersion;
    std::uint32_t headerSize;
    std::uint32_t byteOrder;
    std::uint32_t reserved0;
};

struct ResultTableIndexFileHeader
{
    char magic[8];
    std::uint32_t formatVersion;
    std::uint32_t headerSize;
    std::uint32_t recordSize;
    std::uint32_t reserved0;
};

struct ResultTableRecordDiskHeader
{
    std::uint32_t recordMagic;
    std::uint32_t recordVersion;
    std::uint32_t payloadSizeBytes;
    std::uint32_t crc32;
};

struct ResultTableIndexRecord
{
    std::int64_t resultTimeUtcNs;
    std::uint64_t sampleIndex;
    std::uint64_t fileOffset;
    std::uint32_t recordByteSize;
    std::int32_t bandIndex;
};

#pragma pack(pop)

static_assert(sizeof(ResultTableRecordDiskHeader) == 16);
static_assert(sizeof(ResultTableIndexRecord) == 32);

} // namespace siriusscope::infrastructure::result_table_storage_format
