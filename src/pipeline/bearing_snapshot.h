#pragma once

#include <cstdint>
#include <vector>

namespace siriusscope::pipeline {

struct BearingEstimate
{
    int bandIndex = 0;
    std::int64_t frequencyHz = 0;
    std::uint32_t frequencyBin = 0;
    std::uint64_t sampleIndex = 0;
    std::int64_t resultUtcNs = 0;
    double antennaAzimuthDeg = 0.0;
    double bearingAzimuthDeg = 0.0;
    double quality = 0.0;
    std::uint16_t beam0Peak = 0;
    std::uint16_t beam1Peak = 0;
};

struct BearingSnapshotCounters
{
    std::uint64_t completeCandidates = 0;
    std::uint64_t incompleteCandidates = 0;
    std::uint64_t missingBeam0Candidates = 0;
    std::uint64_t missingBeam1Candidates = 0;
    std::uint64_t producedEstimates = 0;
};

struct BearingSnapshot
{
    std::uint64_t sequenceId = 0;
    std::int64_t createdUtcNs = 0;
    std::vector<BearingEstimate> estimates;
    BearingSnapshotCounters counters;
};

std::int64_t currentBearingSnapshotUtcNs();

} // namespace siriusscope::pipeline
