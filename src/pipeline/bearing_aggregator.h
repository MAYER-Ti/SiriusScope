#pragma once

#include "core/domain_models.h"
#include "pipeline/bearing_snapshot.h"
#include "pipeline/signal_block.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

struct BearingAggregatorConfig
{
    int frequencyBinCount = 1024;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    std::uint64_t windowPeriodNs = 20'000'000;
    int amplitudeFloor = 1;
    double beamHalfSeparationDeg = 30.0;
    double minQuality = 0.05;
    double fallbackAntennaAzimuthDeg = 0.0;
    core::TimeBase timeBase{};
};

struct BearingCandidateAggregate
{
    int bandIndex = 0;
    std::uint32_t frequencyBin = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
    std::int64_t centerFrequencyHz = 0;
    std::uint16_t beam0Peak = 0;
    std::uint16_t beam1Peak = 0;
    bool hasBeam0 = false;
    bool hasBeam1 = false;
    bool hasSamples = false;
    double antennaAzimuthDeg = 0.0;
};

struct BearingAggregatorCounters
{
    std::uint64_t consumedBlocks = 0;
    std::uint64_t consumedSamples = 0;
    std::uint64_t producedSnapshots = 0;
    std::uint64_t producedEstimates = 0;
    std::uint64_t completeCandidates = 0;
    std::uint64_t incompleteCandidates = 0;
    std::uint64_t missingBeam0Candidates = 0;
    std::uint64_t missingBeam1Candidates = 0;
    std::uint64_t invalidSamples = 0;
    std::uint64_t outOfRangeSamples = 0;
};

struct BearingAggregationResult
{
    std::vector<std::shared_ptr<const BearingSnapshot>> snapshots;
    BearingAggregatorCounters deltaCounters;
};

class BearingAggregator
{
public:
    explicit BearingAggregator(BearingAggregatorConfig config = {});

    void reset();
    void setConfig(BearingAggregatorConfig config);
    const BearingAggregatorConfig& config() const noexcept { return m_config; }

    BearingAggregationResult consume(const SignalBlock& block);
    BearingAggregationResult consume(std::span<const core::SignalSample> samples);
    BearingAggregationResult flush();

    BearingAggregatorCounters counters() const noexcept { return m_counters; }

private:
    struct CandidateKey
    {
        int bandIndex = 0;
        std::uint32_t frequencyBin = 0;

        bool operator<(const CandidateKey& other) const noexcept;
    };

    struct OpenWindow
    {
        std::uint64_t windowIndex = 0;
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t lastSampleIndex = 0;
        double antennaAzimuthDeg = 0.0;
        std::map<CandidateKey, BearingCandidateAggregate> candidates;
    };

    BearingAggregationResult consumeSamples(std::span<const core::SignalSample> samples,
                                            std::optional<double> antennaAzimuthDeg);
    std::optional<std::uint64_t> windowForSample(std::uint64_t sampleIndex) const;
    std::int64_t utcNsForSample(std::uint64_t sampleIndex) const;
    int binForFrequency(std::int64_t frequencyHz) const noexcept;
    std::int64_t centerFrequencyForBin(std::uint32_t bin) const noexcept;
    static double normalizeAzimuthDeg(double value) noexcept;
    static std::uint16_t clampAmplitude(int amplitude) noexcept;

    void openWindow(std::uint64_t windowIndex,
                    std::uint64_t sampleIndex,
                    double antennaAzimuthDeg);
    void addSampleToOpenWindow(const core::SignalSample& sample, int binIndex);
    std::shared_ptr<const BearingSnapshot> closeOpenWindow(
        BearingAggregatorCounters& deltaCounters);
    std::optional<BearingEstimate> makeEstimate(
        const BearingCandidateAggregate& candidate) const;

    BearingAggregatorConfig m_config;
    BearingAggregatorCounters m_counters;
    std::optional<OpenWindow> m_openWindow;
    std::uint64_t m_nextSnapshotSequenceId = 1;
};

} // namespace siriusscope::pipeline
