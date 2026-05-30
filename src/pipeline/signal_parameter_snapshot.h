#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::pipeline {

struct BandSignalParametersSummary
{
    int bandIndex = 0;
    std::vector<std::int64_t> frequenciesHz;
    std::optional<double> pulseRepetitionPeriodUs;
    std::optional<double> pulseWidthUs;
    std::uint64_t pulseCount = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
};

struct SignalParameterSnapshot
{
    std::uint64_t sequenceId = 0;
    std::int64_t createdUtcNs = 0;
    std::uint64_t acceptedSampleCount = 0;
    std::uint64_t rejectedSampleCount = 0;
    std::uint64_t pulseCount = 0;
    std::vector<BandSignalParametersSummary> bands;
};

std::int64_t currentSignalParameterSnapshotUtcNs();

} // namespace siriusscope::pipeline
