#include "processing/signal_parameter_accumulator.h"

#include <algorithm>
#include <utility>

namespace siriusscope::processing {
namespace {

bool hasValidAmplitude(const core::SignalSample& sample)
{
    return core::validateAmplitude(sample.amplitude).isValid();
}

bool hasValidBandIndex(const core::SignalSample& sample)
{
    return core::validateBandIndex(sample.bandIndex, core::defaultRuntimeCapabilities()).isValid();
}

bool hasValidBeamIndex(const core::SignalSample& sample)
{
    return core::validateBeamIndex(sample.beamIndex, core::defaultRuntimeCapabilities()).isValid();
}

bool hasValidFrequency(const core::SignalSample& sample)
{
    return core::validateSystemFrequency(sample.absoluteFrequencyHz).isValid();
}

bool hasValidSample(const core::SignalSample& sample)
{
    return hasValidAmplitude(sample) && hasValidBandIndex(sample)
        && hasValidBeamIndex(sample) && hasValidFrequency(sample);
}

bool sampleLess(const core::SignalSample& lhs, const core::SignalSample& rhs)
{
    if (lhs.bandIndex != rhs.bandIndex) {
        return lhs.bandIndex < rhs.bandIndex;
    }
    if (lhs.sampleIndex != rhs.sampleIndex) {
        return lhs.sampleIndex < rhs.sampleIndex;
    }
    if (lhs.absoluteFrequencyHz != rhs.absoluteFrequencyHz) {
        return lhs.absoluteFrequencyHz < rhs.absoluteFrequencyHz;
    }
    return lhs.beamIndex < rhs.beamIndex;
}

double samplesToMicroseconds(std::uint64_t sampleCount, std::uint64_t samplePeriodNs)
{
    return static_cast<double>(sampleCount) * static_cast<double>(samplePeriodNs) / 1000.0;
}

} // namespace

SignalParameterAccumulator::CurrentPulse SignalParameterAccumulator::makePulse(
    const core::SignalSample& sample)
{
    return CurrentPulse{
        sample.bandIndex,
        sample.sampleIndex,
        sample.sampleIndex,
        sample.sampleIndex,
        sample.amplitude,
        1,
        sample.absoluteFrequencyHz,
    };
}

void SignalParameterAccumulator::appendSample(CurrentPulse& pulse,
                                              const core::SignalSample& sample)
{
    pulse.lastSampleIndex = std::max(pulse.lastSampleIndex, sample.sampleIndex);
    pulse.lastSeenSampleIndex = sample.sampleIndex;
    ++pulse.sampleCount;
    if (sample.amplitude > pulse.peakAmplitude) {
        pulse.peakAmplitude = sample.amplitude;
        pulse.representativeFrequencyHz = sample.absoluteFrequencyHz;
    }
}

bool SignalParameterAccumulator::shouldKeepPulse(
    const CurrentPulse& pulse,
    const SignalParameterEstimatorConfig& config) noexcept
{
    if (pulse.sampleCount < config.minSamplesPerPulse
        || pulse.lastSampleIndex < pulse.firstSampleIndex) {
        return false;
    }

    const auto widthSamples = pulse.lastSampleIndex - pulse.firstSampleIndex + 1;
    return config.maxPulseWidthSamples == 0 || widthSamples <= config.maxPulseWidthSamples;
}

void SignalParameterAccumulator::closePulse(BandSignalAccumulator& state) const
{
    if (!state.currentPulse) {
        return;
    }

    const auto pulse = *state.currentPulse;
    state.currentPulse.reset();
    if (!shouldKeepPulse(pulse, m_config)) {
        return;
    }

    const auto widthSamples = pulse.lastSampleIndex - pulse.firstSampleIndex + 1;
    state.pulseWidthSumUs += samplesToMicroseconds(widthSamples, m_config.samplePeriodNs);

    if (state.previousPulseFirstSampleIndex) {
        const auto priSamples = pulse.firstSampleIndex - *state.previousPulseFirstSampleIndex;
        state.pulseRepetitionPeriodSumUs += samplesToMicroseconds(priSamples,
                                                                  m_config.samplePeriodNs);
        ++state.pulseRepetitionPeriodCount;
    }
    state.previousPulseFirstSampleIndex = pulse.firstSampleIndex;

    ++state.pulseCount;
    state.frequenciesHz.push_back(pulse.representativeFrequencyHz);
}

std::size_t SignalParameterAccumulator::finalizedPulseCount(
    const BandSignalAccumulator& state) const noexcept
{
    if (state.currentPulse && shouldKeepPulse(*state.currentPulse, m_config)) {
        return state.pulseCount + 1;
    }
    return state.pulseCount;
}

SignalParameterAccumulator::SignalParameterAccumulator(SignalParameterEstimatorConfig config)
    : m_config(std::move(config))
{
    m_config.samplePeriodNs = std::max<std::uint64_t>(1, m_config.samplePeriodNs);
    m_config.maxIntraPulseGapSamples =
        std::max<std::uint64_t>(1, m_config.maxIntraPulseGapSamples);
    m_config.minSamplesPerPulse = std::max<std::size_t>(1, m_config.minSamplesPerPulse);
}

void SignalParameterAccumulator::reset()
{
    m_bands.clear();
    m_acceptedSampleCount = 0;
    m_rejectedSampleCount = 0;
}

void SignalParameterAccumulator::ingest(const std::vector<core::SignalSample>& samples)
{
    ingest(std::span<const core::SignalSample>{samples.data(), samples.size()});
}

void SignalParameterAccumulator::ingest(std::span<const core::SignalSample> samples)
{
    if (samples.empty()) {
        return;
    }

    std::vector<core::SignalSample> orderedSamples(samples.begin(), samples.end());
    std::sort(orderedSamples.begin(), orderedSamples.end(), sampleLess);

    for (const auto& sample : orderedSamples) {
        if (!hasValidSample(sample)) {
            ++m_rejectedSampleCount;
            continue;
        }

        auto& state = m_bands[sample.bandIndex];
        if (state.lastAcceptedSampleIndex && sample.sampleIndex < *state.lastAcceptedSampleIndex) {
            ++m_rejectedSampleCount;
            continue;
        }

        ++m_acceptedSampleCount;
        state.lastAcceptedSampleIndex = sample.sampleIndex;

        if (!state.currentPulse) {
            state.currentPulse = makePulse(sample);
            continue;
        }

        const auto previousSampleIndex = state.currentPulse->lastSeenSampleIndex;
        const auto gap =
            sample.sampleIndex > previousSampleIndex ? sample.sampleIndex - previousSampleIndex : 0;
        if (gap == 0 || gap <= m_config.maxIntraPulseGapSamples) {
            appendSample(*state.currentPulse, sample);
            continue;
        }

        closePulse(state);
        state.currentPulse = makePulse(sample);
    }
}

std::vector<SignalParameters> SignalParameterAccumulator::finalize() const
{
    std::vector<SignalParameters> result;
    result.reserve(m_bands.size());

    for (const auto& [bandIndex, sourceState] : m_bands) {
        auto state = sourceState;
        closePulse(state);
        if (state.pulseCount == 0) {
            continue;
        }

        auto frequenciesHz = std::move(state.frequenciesHz);
        if (m_config.uniqueFrequencies) {
            std::sort(frequenciesHz.begin(), frequenciesHz.end());
            frequenciesHz.erase(std::unique(frequenciesHz.begin(), frequenciesHz.end()),
                                frequenciesHz.end());
        }

        std::optional<double> priUs;
        if (state.pulseRepetitionPeriodCount > 0) {
            priUs = state.pulseRepetitionPeriodSumUs
                / static_cast<double>(state.pulseRepetitionPeriodCount);
        }

        result.push_back(SignalParameters{
            bandIndex,
            state.pulseCount,
            state.pulseWidthSumUs / static_cast<double>(state.pulseCount),
            priUs,
            std::move(frequenciesHz),
        });
    }

    return result;
}

std::size_t SignalParameterAccumulator::acceptedSampleCount() const noexcept
{
    return m_acceptedSampleCount;
}

std::size_t SignalParameterAccumulator::rejectedSampleCount() const noexcept
{
    return m_rejectedSampleCount;
}

std::size_t SignalParameterAccumulator::pulseCount() const noexcept
{
    std::size_t count = 0;
    for (const auto& [bandIndex, state] : m_bands) {
        (void) bandIndex;
        count += finalizedPulseCount(state);
    }
    return count;
}

} // namespace siriusscope::processing
