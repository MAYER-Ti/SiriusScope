#pragma once

#include <cstdint>
#include <vector>

namespace siriusscope::pipeline {

struct WaterfallAggregateCell
{
    std::uint16_t beam0Peak = 0;
    std::uint16_t beam1Peak = 0;
    std::uint16_t hitCount = 0;
};

struct WaterfallSnapshotCounters
{
    std::uint64_t producedRows = 0;
    std::uint64_t producedSnapshots = 0;
    std::uint64_t invalidFrequencySamples = 0;
    std::uint64_t outOfRangeSamples = 0;
    std::uint64_t emptyBlocks = 0;
};

struct WaterfallSnapshotRow
{
    std::int64_t utcNs = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
    std::vector<WaterfallAggregateCell> cells;
};

struct WaterfallSnapshot
{
    std::uint64_t sequenceId = 0;
    std::int64_t createdAtUtcNs = 0;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    int renderBinCount = 1024;
    std::uint64_t rowPeriodNs = 20'000'000;
    std::vector<WaterfallSnapshotRow> rows;
    WaterfallSnapshotCounters counters;
};

std::int64_t currentUtcNs();

} // namespace siriusscope::pipeline
