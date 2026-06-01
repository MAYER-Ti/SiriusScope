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

bool hasTrustedMapBandIndexStorageRange(int bandIndex)
{
    const auto defaultBandCount = core::defaultRuntimeCapabilities().bandCount;
    return bandIndex >= 0 && defaultBandCount > 0 && bandIndex < defaultBandCount;
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
    recordPulse(state, pulse);
}

void SignalParameterAccumulator::recordPulse(BandSignalAccumulator& state,
                                             const CurrentPulse& pulse) const
{
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
    recordRepresentativeFrequency(state.frequenciesHz, pulse.representativeFrequencyHz);
}

void SignalParameterAccumulator::recordRepresentativeFrequency(
    std::vector<std::int64_t>& frequenciesHz,
    std::int64_t frequencyHz) const
{
    if (!m_config.uniqueFrequencies) {
        frequenciesHz.push_back(frequencyHz);
        return;
    }

    const auto insertion = std::lower_bound(frequenciesHz.begin(),
                                            frequenciesHz.end(),
                                            frequencyHz);
    if (insertion == frequenciesHz.end() || *insertion != frequencyHz) {
        frequenciesHz.insert(insertion, frequencyHz);
    }
}

std::size_t SignalParameterAccumulator::finalizedPulseCount(
    const BandSignalAccumulator& state) const noexcept
{
    if (state.currentPulse && shouldKeepPulse(*state.currentPulse, m_config)) {
        return state.pulseCount + 1;
    }
    return state.pulseCount;
}

void SignalParameterAccumulator::prepareBandVectorStorage()
{
    if (m_config.bandStateMode != SignalParameterBandStateMode::FixedBandIndexVector) {
        m_bandVector.clear();
        m_bandVectorUsed.clear();
        return;
    }

    if (m_config.bandStateCapacity == 0) {
        const auto defaultBandCount = core::defaultRuntimeCapabilities().bandCount;
        if (defaultBandCount > 0) {
            m_config.bandStateCapacity = static_cast<std::size_t>(defaultBandCount);
        }
    }

    m_bandVector.resize(m_config.bandStateCapacity);
    m_bandVectorUsed.assign(m_config.bandStateCapacity, false);
}

SignalParameterAccumulator::BandSignalAccumulator* SignalParameterAccumulator::stateForBand(
    int bandIndex)
{
    if (m_config.bandStateMode == SignalParameterBandStateMode::FixedBandIndexVector) {
        if (bandIndex < 0) {
            return nullptr;
        }

        const auto vectorIndex = static_cast<std::size_t>(bandIndex);
        if (vectorIndex >= m_config.bandStateCapacity
            || vectorIndex >= m_bandVector.size()
            || vectorIndex >= m_bandVectorUsed.size()) {
            return nullptr;
        }

        m_bandVectorUsed[vectorIndex] = true;
        return &m_bandVector[vectorIndex];
    }

    return &m_bands[bandIndex];
}

const SignalParameterAccumulator::BandSignalAccumulator*
SignalParameterAccumulator::stateForBandIfUsed(int bandIndex) const
{
    if (m_config.bandStateMode == SignalParameterBandStateMode::FixedBandIndexVector) {
        if (bandIndex < 0) {
            return nullptr;
        }

        const auto vectorIndex = static_cast<std::size_t>(bandIndex);
        if (vectorIndex >= m_bandVector.size() || vectorIndex >= m_bandVectorUsed.size()
            || !m_bandVectorUsed[vectorIndex]) {
            return nullptr;
        }

        return &m_bandVector[vectorIndex];
    }

    const auto found = m_bands.find(bandIndex);
    return found == m_bands.end() ? nullptr : &found->second;
}

template <typename Fn>
void SignalParameterAccumulator::forEachUsedBandState(Fn&& fn) const
{
    if (m_config.bandStateMode == SignalParameterBandStateMode::FixedBandIndexVector) {
        for (std::size_t index = 0; index < m_bandVector.size(); ++index) {
            if (index < m_bandVectorUsed.size() && m_bandVectorUsed[index]) {
                fn(static_cast<int>(index), m_bandVector[index]);
            }
        }
        return;
    }

    for (const auto& [bandIndex, state] : m_bands) {
        fn(bandIndex, state);
    }
}

SignalParameterAccumulator::SignalParameterAccumulator(SignalParameterEstimatorConfig config)
    : m_config(std::move(config))
{
    m_config.samplePeriodNs = std::max<std::uint64_t>(1, m_config.samplePeriodNs);
    m_config.maxIntraPulseGapSamples =
        std::max<std::uint64_t>(1, m_config.maxIntraPulseGapSamples);
    m_config.minSamplesPerPulse = std::max<std::size_t>(1, m_config.minSamplesPerPulse);
    prepareBandVectorStorage();
}

void SignalParameterAccumulator::reset()
{
    m_bands.clear();
    if (m_config.bandStateMode == SignalParameterBandStateMode::FixedBandIndexVector) {
        if (m_bandVector.size() != m_config.bandStateCapacity
            || m_bandVectorUsed.size() != m_config.bandStateCapacity) {
            prepareBandVectorStorage();
        }
        for (auto& state : m_bandVector) {
            state = {};
        }
        std::fill(m_bandVectorUsed.begin(), m_bandVectorUsed.end(), false);
    } else {
        m_bandVector.clear();
        m_bandVectorUsed.clear();
    }
    m_acceptedSampleCount = 0;
    m_rejectedSampleCount = 0;
}

void SignalParameterAccumulator::ingest(const std::vector<core::SignalSample>& samples)
{
    ingest(std::span<const core::SignalSample>{samples.data(), samples.size()});
}

void SignalParameterAccumulator::ingest(std::span<const core::SignalSample> samples)
{
    if (m_config.ingestMode == SignalParameterIngestMode::Streaming) {
        ingestStreaming(samples);
        return;
    }

    ingestSorted(samples);
}

void SignalParameterAccumulator::ingestStreaming(std::span<const core::SignalSample> samples)
{
    if (m_config.validationMode == SignalParameterValidationMode::TrustedValidatedSamples) {
        for (const auto& sample : samples) {
            ingestOneTrustedSample(sample);
        }
        return;
    }

    for (const auto& sample : samples) {
        ingestOneSample(sample);
    }
}

void SignalParameterAccumulator::ingestSorted(std::span<const core::SignalSample> samples)
{
    if (samples.empty()) {
        return;
    }

    std::vector<core::SignalSample> orderedSamples(samples.begin(), samples.end());
    std::sort(orderedSamples.begin(), orderedSamples.end(), sampleLess);

    for (const auto& sample : orderedSamples) {
        if (m_config.validationMode == SignalParameterValidationMode::TrustedValidatedSamples) {
            ingestOneTrustedSample(sample);
        } else {
            ingestOneSample(sample);
        }
    }
}

void SignalParameterAccumulator::ingestOneSample(const core::SignalSample& sample)
{
    if (!hasValidSample(sample)) {
        ++m_rejectedSampleCount;
        return;
    }

    auto* state = stateForBand(sample.bandIndex);
    if (!state) {
        ++m_rejectedSampleCount;
        return;
    }

    ingestValidSample(*state, sample);
}

void SignalParameterAccumulator::ingestOneTrustedSample(const core::SignalSample& sample)
{
    if (m_config.bandStateMode == SignalParameterBandStateMode::MapByBandIndex
        && !hasTrustedMapBandIndexStorageRange(sample.bandIndex)) {
        ++m_rejectedSampleCount;
        return;
    }

    auto* state = stateForBand(sample.bandIndex);
    if (!state) {
        ++m_rejectedSampleCount;
        return;
    }

    ingestValidSample(*state, sample);
}

void SignalParameterAccumulator::ingestValidSample(BandSignalAccumulator& state,
                                                   const core::SignalSample& sample)
{
    if (state.lastAcceptedSampleIndex && sample.sampleIndex < *state.lastAcceptedSampleIndex) {
        ++m_rejectedSampleCount;
        return;
    }

    ++m_acceptedSampleCount;
    state.lastAcceptedSampleIndex = sample.sampleIndex;

    if (!state.currentPulse) {
        state.currentPulse = makePulse(sample);
        return;
    }

    const auto previousSampleIndex = state.currentPulse->lastSeenSampleIndex;
    const auto gap =
        sample.sampleIndex > previousSampleIndex ? sample.sampleIndex - previousSampleIndex : 0;
    if (gap == 0 || gap <= m_config.maxIntraPulseGapSamples) {
        appendSample(*state.currentPulse, sample);
        return;
    }

    closePulse(state);
    state.currentPulse = makePulse(sample);
}

std::vector<SignalParameters> SignalParameterAccumulator::finalize() const
{
    std::vector<SignalParameters> result;
    result.reserve(m_config.bandStateMode == SignalParameterBandStateMode::FixedBandIndexVector
                       ? m_bandVector.size()
                       : m_bands.size());

    forEachUsedBandState([&](int bandIndex, const BandSignalAccumulator& sourceState) {
        auto pulseCount = sourceState.pulseCount;
        auto pulseWidthSumUs = sourceState.pulseWidthSumUs;
        auto pulseRepetitionPeriodSumUs = sourceState.pulseRepetitionPeriodSumUs;
        auto pulseRepetitionPeriodCount = sourceState.pulseRepetitionPeriodCount;
        auto previousPulseFirstSampleIndex = sourceState.previousPulseFirstSampleIndex;
        auto frequenciesHz = sourceState.frequenciesHz;

        if (sourceState.currentPulse && shouldKeepPulse(*sourceState.currentPulse, m_config)) {
            const auto& pulse = *sourceState.currentPulse;
            const auto widthSamples = pulse.lastSampleIndex - pulse.firstSampleIndex + 1;
            pulseWidthSumUs += samplesToMicroseconds(widthSamples, m_config.samplePeriodNs);

            if (previousPulseFirstSampleIndex) {
                const auto priSamples = pulse.firstSampleIndex - *previousPulseFirstSampleIndex;
                pulseRepetitionPeriodSumUs += samplesToMicroseconds(priSamples,
                                                                    m_config.samplePeriodNs);
                ++pulseRepetitionPeriodCount;
            }

            ++pulseCount;
            recordRepresentativeFrequency(frequenciesHz, pulse.representativeFrequencyHz);
        }

        if (pulseCount == 0) {
            return;
        }

        std::optional<double> priUs;
        if (pulseRepetitionPeriodCount > 0) {
            priUs = pulseRepetitionPeriodSumUs / static_cast<double>(pulseRepetitionPeriodCount);
        }

        result.push_back(SignalParameters{
            bandIndex,
            pulseCount,
            pulseWidthSumUs / static_cast<double>(pulseCount),
            priUs,
            std::move(frequenciesHz),
        });
    });

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
    forEachUsedBandState([&](int bandIndex, const BandSignalAccumulator& state) {
        (void) bandIndex;
        count += finalizedPulseCount(state);
    });
    return count;
}

} // namespace siriusscope::processing
