#pragma once

#include "core/domain_models.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::processing {

struct SignalPulse
{
    int bandIndex = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
    int peakAmplitude = 0;
    std::size_t sampleCount = 0;
    std::int64_t representativeFrequencyHz = 0;
};

struct SignalParameters
{
    int bandIndex = 0;
    std::size_t pulseCount = 0;
    double pulseWidthUs = 0.0;
    std::optional<double> pulseRepetitionPeriodUs;
    std::vector<std::int64_t> frequenciesHz;
};

enum class PulseGroupingMode
{
    GapThreshold,
    AdaptiveGap,
};

enum class SignalParameterIngestMode
{
    SortByBandAndSample,
    Streaming,
};

struct SignalParameterEstimatorConfig
{
    std::uint64_t samplePeriodNs = core::DomainConstraints::defaultSamplePeriodNs;
    std::uint64_t maxIntraPulseGapSamples = 1;
    std::size_t minSamplesPerPulse = 1;
    bool uniqueFrequencies = true;
    PulseGroupingMode groupingMode = PulseGroupingMode::AdaptiveGap;
    SignalParameterIngestMode ingestMode = SignalParameterIngestMode::SortByBandAndSample;
    std::uint64_t minInterPulseGapSamples = 0;
    std::uint64_t maxPulseWidthSamples = 0;
};

class SignalParameterEstimator
{
public:
    explicit SignalParameterEstimator(SignalParameterEstimatorConfig config = {});

    std::vector<SignalPulse> buildPulses(const std::vector<core::SignalSample>& samples) const;
    std::vector<SignalParameters> estimate(const std::vector<core::SignalSample>& samples) const;

private:
    SignalParameterEstimatorConfig m_config;
};

} // namespace siriusscope::processing
