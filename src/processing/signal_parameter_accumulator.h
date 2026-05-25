#pragma once

#include "processing/signal_parameter_estimator.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace siriusscope::processing {

class SignalParameterAccumulator
{
public:
    explicit SignalParameterAccumulator(SignalParameterEstimatorConfig config = {});

    void reset();
    void ingest(const std::vector<core::SignalSample>& samples);
    void ingest(std::span<const core::SignalSample> samples);
    std::vector<SignalParameters> finalize() const;

    std::size_t acceptedSampleCount() const noexcept;
    std::size_t rejectedSampleCount() const noexcept;
    std::size_t pulseCount() const noexcept;

private:
    struct CurrentPulse
    {
        int bandIndex = 0;
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t lastSampleIndex = 0;
        std::uint64_t lastSeenSampleIndex = 0;
        int peakAmplitude = 0;
        std::size_t sampleCount = 0;
        std::int64_t representativeFrequencyHz = 0;
    };

    struct BandSignalAccumulator
    {
        std::optional<CurrentPulse> currentPulse;
        std::optional<std::uint64_t> previousPulseFirstSampleIndex;
        std::optional<std::uint64_t> lastAcceptedSampleIndex;

        std::size_t pulseCount = 0;
        double pulseWidthSumUs = 0.0;

        double pulseRepetitionPeriodSumUs = 0.0;
        std::size_t pulseRepetitionPeriodCount = 0;

        std::vector<std::int64_t> frequenciesHz;
    };

    static CurrentPulse makePulse(const core::SignalSample& sample);
    static void appendSample(CurrentPulse& pulse, const core::SignalSample& sample);
    static bool shouldKeepPulse(const CurrentPulse& pulse,
                                const SignalParameterEstimatorConfig& config) noexcept;
    void closePulse(BandSignalAccumulator& state) const;
    std::size_t finalizedPulseCount(const BandSignalAccumulator& state) const noexcept;

    SignalParameterEstimatorConfig m_config;
    std::map<int, BandSignalAccumulator> m_bands;
    std::size_t m_acceptedSampleCount = 0;
    std::size_t m_rejectedSampleCount = 0;
};

} // namespace siriusscope::processing
