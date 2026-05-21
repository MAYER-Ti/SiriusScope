#include "processing/signal_parameter_estimator.h"

#include <algorithm>
#include <map>
#include <utility>

namespace siriusscope::processing {
namespace {

bool hasValidAmplitude(const core::SignalSample& sample)
{
    return sample.amplitude >= core::DomainConstraints::minAmplitude
        && sample.amplitude <= core::DomainConstraints::maxAmplitude;
}

bool hasValidBandIndex(const core::SignalSample& sample)
{
    return core::validateBandIndex(sample.bandIndex, core::defaultRuntimeCapabilities()).isValid();
}

bool sampleLess(const core::SignalSample& lhs, const core::SignalSample& rhs)
{
    if (lhs.sampleIndex != rhs.sampleIndex) {
        return lhs.sampleIndex < rhs.sampleIndex;
    }
    if (lhs.absoluteFrequencyHz != rhs.absoluteFrequencyHz) {
        return lhs.absoluteFrequencyHz < rhs.absoluteFrequencyHz;
    }
    return lhs.beamIndex < rhs.beamIndex;
}

SignalPulse makePulse(const core::SignalSample& sample)
{
    return SignalPulse{
        sample.bandIndex,
        sample.sampleIndex,
        sample.sampleIndex,
        sample.amplitude,
        1,
        sample.absoluteFrequencyHz,
    };
}

void appendSample(SignalPulse& pulse, const core::SignalSample& sample)
{
    pulse.lastSampleIndex = sample.sampleIndex;
    ++pulse.sampleCount;
    if (sample.amplitude > pulse.peakAmplitude) {
        pulse.peakAmplitude = sample.amplitude;
        pulse.representativeFrequencyHz = sample.absoluteFrequencyHz;
    }
}

double samplesToMicroseconds(std::uint64_t sampleCount, std::uint64_t samplePeriodNs)
{
    return static_cast<double>(sampleCount) * static_cast<double>(samplePeriodNs) / 1000.0;
}

} // namespace

SignalParameterEstimator::SignalParameterEstimator(SignalParameterEstimatorConfig config)
    : m_config(std::move(config))
{
    m_config.samplePeriodNs = std::max<std::uint64_t>(1, m_config.samplePeriodNs);
    m_config.maxIntraPulseGapSamples =
        std::max<std::uint64_t>(1, m_config.maxIntraPulseGapSamples);
    m_config.minSamplesPerPulse = std::max<std::size_t>(1, m_config.minSamplesPerPulse);
}

std::vector<SignalPulse> SignalParameterEstimator::buildPulses(
    const std::vector<core::SignalSample>& samples) const
{
    std::map<int, std::vector<core::SignalSample>> samplesByBand;
    for (const auto& sample : samples) {
        if (!hasValidAmplitude(sample) || !hasValidBandIndex(sample)) {
            continue;
        }

        samplesByBand[sample.bandIndex].push_back(sample);
    }

    std::vector<SignalPulse> pulses;
    for (auto& [bandIndex, bandSamples] : samplesByBand) {
        std::sort(bandSamples.begin(), bandSamples.end(), sampleLess);
        if (bandSamples.empty()) {
            continue;
        }

        auto currentPulse = makePulse(bandSamples.front());
        auto previousSampleIndex = bandSamples.front().sampleIndex;
        for (std::size_t i = 1; i < bandSamples.size(); ++i) {
            const auto& sample = bandSamples[i];
            if (sample.sampleIndex - previousSampleIndex <= m_config.maxIntraPulseGapSamples) {
                appendSample(currentPulse, sample);
            } else {
                if (currentPulse.sampleCount >= m_config.minSamplesPerPulse) {
                    pulses.push_back(currentPulse);
                }
                currentPulse = makePulse(sample);
            }
            previousSampleIndex = sample.sampleIndex;
        }

        if (currentPulse.sampleCount >= m_config.minSamplesPerPulse) {
            pulses.push_back(currentPulse);
        }
    }

    return pulses;
}

std::vector<SignalParameters> SignalParameterEstimator::estimate(
    const std::vector<core::SignalSample>& samples) const
{
    const auto pulses = buildPulses(samples);

    std::map<int, std::vector<SignalPulse>> pulsesByBand;
    for (const auto& pulse : pulses) {
        pulsesByBand[pulse.bandIndex].push_back(pulse);
    }

    std::vector<SignalParameters> estimates;
    estimates.reserve(pulsesByBand.size());
    for (const auto& [bandIndex, bandPulses] : pulsesByBand) {
        if (bandPulses.empty()) {
            continue;
        }

        double widthSumUs = 0.0;
        std::vector<std::int64_t> frequencies;
        frequencies.reserve(bandPulses.size());
        for (const auto& pulse : bandPulses) {
            widthSumUs += samplesToMicroseconds(pulse.lastSampleIndex - pulse.firstSampleIndex + 1,
                                                m_config.samplePeriodNs);
            frequencies.push_back(pulse.representativeFrequencyHz);
        }

        std::optional<double> priUs;
        if (bandPulses.size() >= 2) {
            double priSumUs = 0.0;
            for (std::size_t i = 1; i < bandPulses.size(); ++i) {
                priSumUs += samplesToMicroseconds(
                    bandPulses[i].firstSampleIndex - bandPulses[i - 1].firstSampleIndex,
                    m_config.samplePeriodNs);
            }
            priUs = priSumUs / static_cast<double>(bandPulses.size() - 1);
        }

        std::sort(frequencies.begin(), frequencies.end());
        if (m_config.uniqueFrequencies) {
            frequencies.erase(std::unique(frequencies.begin(), frequencies.end()), frequencies.end());
        }

        estimates.push_back(SignalParameters{
            bandIndex,
            bandPulses.size(),
            widthSumUs / static_cast<double>(bandPulses.size()),
            priUs,
            std::move(frequencies),
        });
    }

    return estimates;
}

} // namespace siriusscope::processing
