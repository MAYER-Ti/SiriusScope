#pragma once

#include <cstdint>
#include <vector>

namespace siriusscope::pipeline {

struct SpectrumBin
{
    std::uint16_t beam0Peak = 0;
    std::uint16_t beam1Peak = 0;
    std::uint16_t totalPeak = 0;
    std::uint16_t hitCount = 0;
};

struct SpectrumBandSummary
{
    int bandIndex = 0;
    std::uint64_t sampleCount = 0;
    std::uint16_t beam0Peak = 0;
    std::uint16_t beam1Peak = 0;
    std::uint16_t totalPeak = 0;
};

struct SpectrumSnapshotCounters
{
    std::uint64_t producedSnapshots = 0;
    std::uint64_t invalidSamples = 0;
    std::uint64_t outOfRangeSamples = 0;
};

struct SpectrumSnapshot
{
    std::uint64_t sequenceId = 0;
    std::int64_t createdUtcNs = 0;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    int renderBinCount = 1024;
    std::uint64_t snapshotPeriodNs = 20'000'000;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
    std::vector<SpectrumBin> bins;
    std::vector<SpectrumBandSummary> bandSummaries;
    SpectrumSnapshotCounters counters;
};

std::int64_t currentSpectrumSnapshotUtcNs();

} // namespace siriusscope::pipeline
