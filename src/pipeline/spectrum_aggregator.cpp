#include "pipeline/spectrum_aggregator.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace siriusscope::pipeline {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::uint64_t kTimingSampleStride = 64;

SpectrumAggregatorConfig normalizeConfig(SpectrumAggregatorConfig config)
{
    config.renderBinCount = std::max(1, config.renderBinCount);

    if (config.bandCapacity == 0) {
        const auto defaultBandCount = core::defaultRuntimeCapabilities().bandCount;
        if (defaultBandCount > 0) {
            config.bandCapacity = static_cast<std::size_t>(defaultBandCount);
        }
    }
    if (config.bandCapacity == 0) {
        config.bandCapacity = 1;
    }

    return config;
}

bool shouldMeasureSampledTiming(std::uint64_t zeroBasedCount) noexcept
{
    return zeroBasedCount % kTimingSampleStride == 0;
}

Clock::duration scaleSampledDuration(Clock::duration sampled,
                                     std::uint64_t sampledCount,
                                     std::uint64_t totalCount)
{
    if (sampledCount == 0 || totalCount == 0) {
        return {};
    }

    const auto sampledNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(sampled).count();
    if (sampledNs <= 0) {
        return {};
    }

    const auto scaledNs =
        static_cast<long double>(sampledNs)
        * static_cast<long double>(totalCount)
        / static_cast<long double>(sampledCount);
    const auto clampedNs = std::min<long double>(
        scaledNs,
        static_cast<long double>(std::numeric_limits<std::int64_t>::max()));
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::nanoseconds{static_cast<std::int64_t>(clampedNs)});
}

} // namespace

SpectrumAggregator::SpectrumAggregator(SpectrumAggregatorConfig config)
    : m_config(normalizeConfig(std::move(config)))
{
    prepareDerivedConfig();
}

void SpectrumAggregator::reset()
{
    m_counters = {};
    m_openWindow.reset();
    m_nextSnapshotSequenceId = 1;
    resetIncrementalWindowState();
    m_incrementalWindowFallbacks = 0;
    resetLocalAccumulationTouched();
}

void SpectrumAggregator::setConfig(SpectrumAggregatorConfig config)
{
    m_config = normalizeConfig(std::move(config));
    prepareDerivedConfig();
    reset();
}

void SpectrumAggregator::prepareDerivedConfig()
{
    m_samplesPerWindow = 0;
    m_canUseFastWindowIndex = false;
    m_canUseIncrementalWindowIndex = false;
    if (m_config.useFastWindowIndex && m_config.timeBase.samplePeriodNs > 0
        && m_config.snapshotPeriodNs > 0
        && m_config.windowIndexMode == SpectrumWindowIndexMode::DivisibleSamplePeriod
        && m_config.snapshotPeriodNs % m_config.timeBase.samplePeriodNs == 0) {
        m_samplesPerWindow =
            m_config.snapshotPeriodNs / m_config.timeBase.samplePeriodNs;
        m_canUseFastWindowIndex = m_samplesPerWindow > 0;
    }
    m_canUseIncrementalWindowIndex =
        m_config.useFastWindowIndex
        && m_config.windowIndexMode == SpectrumWindowIndexMode::IncrementalMonotonic
        && m_config.timeBase.samplePeriodNs > 0
        && m_config.snapshotPeriodNs > 0;

    m_fastBinRangeHz = 0;
    m_fastBinMultiplier = 0;
    m_canUseFastBinIndex = false;
    if (m_config.useFastBinIndex && m_config.renderBinCount > 1
        && m_config.sourceMaxHz > m_config.sourceMinHz) {
        const auto range = m_config.sourceMaxHz - m_config.sourceMinHz;
        const auto multiplier =
            static_cast<std::int64_t>(m_config.renderBinCount - 1);
        if (range > 0 && multiplier > 0
            && range <= std::numeric_limits<std::int64_t>::max() / multiplier) {
            m_fastBinRangeHz = range;
            m_fastBinMultiplier = multiplier;
            m_canUseFastBinIndex = true;
        }
    }
}

bool SpectrumAggregator::usesFixedBandSummaryStorage() const noexcept
{
    return m_config.useFixedBandSummaryStorage;
}

bool SpectrumAggregator::usesBlockLocalFastPath() const noexcept
{
    return m_config.trustedSamples && usesFixedBandSummaryStorage()
        && m_config.renderBinCount > 0 && m_config.bandCapacity > 0;
}

bool SpectrumAggregator::usesTrustedBlockLocalFastPath() const noexcept
{
    return usesBlockLocalFastPath() && !m_config.enableDetailedTiming
        && m_canUseIncrementalWindowIndex && m_canUseFastBinIndex
        && m_config.sourceMaxHz > m_config.sourceMinHz;
}

bool SpectrumAggregator::hasValidUntrustedSample(
    const core::SignalSample& sample) const
{
    return core::validateAmplitude(sample.amplitude).isValid()
        && core::validateBandIndex(sample.bandIndex).isValid()
        && core::validateBeamIndex(sample.beamIndex).isValid()
        && core::validateSystemFrequency(sample.absoluteFrequencyHz).isValid();
}

SpectrumAggregationResult SpectrumAggregator::consumeTrustedBlockLocalFast(
    std::span<const core::SignalSample> samples)
{
    SpectrumAggregationResult result;
    const auto totalStartedAt = Clock::now();
    const auto incrementalFallbacksBefore = m_incrementalWindowFallbacks;

    result.usedFastWindowIndex = m_canUseFastWindowIndex;
    result.usedIncrementalWindowIndex = m_canUseIncrementalWindowIndex;
    result.usedFastBinIndex = m_canUseFastBinIndex;
    result.usedFastBandSummaryStorage = true;
    result.usedBlockLocalAccumulation = !samples.empty();
    result.snapshots.reserve(2);
    prepareLocalAccumulationBuffers();

    const auto sampleLoopStartedAt = Clock::now();
    for (const auto& sample : samples) {
        ++m_counters.consumedSamples;
        ++result.deltaCounters.consumedSamples;

        if (sample.sampleIndex < m_config.timeBase.firstSampleIndex
            || sample.amplitude <= 0) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }

        std::optional<std::uint64_t> fallbackWindow;
        if (m_lastWindowSampleIndex
            && sample.sampleIndex < *m_lastWindowSampleIndex) {
            ++m_incrementalWindowFallbacks;
            resetIncrementalWindowState();
            fallbackWindow = exactWindowForSample(sample.sampleIndex);
            primeIncrementalWindowState(sample.sampleIndex, fallbackWindow);
        } else if (!m_incrementalWindowIndex
                   || !m_incrementalNextWindowStartSampleIndex) {
            fallbackWindow = exactWindowForSample(sample.sampleIndex);
            primeIncrementalWindowState(sample.sampleIndex, fallbackWindow);
        } else {
            while (m_incrementalNextWindowStartSampleIndex
                   && sample.sampleIndex >= *m_incrementalNextWindowStartSampleIndex) {
                if (*m_incrementalWindowIndex
                    == std::numeric_limits<std::uint64_t>::max()) {
                    ++m_incrementalWindowFallbacks;
                    fallbackWindow = exactWindowForSample(sample.sampleIndex);
                    primeIncrementalWindowState(sample.sampleIndex, fallbackWindow);
                    break;
                }

                ++(*m_incrementalWindowIndex);
                if (*m_incrementalWindowIndex
                    == std::numeric_limits<std::uint64_t>::max()) {
                    m_incrementalNextWindowStartSampleIndex.reset();
                    break;
                }

                const auto nextBoundary =
                    firstSampleIndexForWindow(*m_incrementalWindowIndex + 1);
                if (!nextBoundary) {
                    ++m_incrementalWindowFallbacks;
                    fallbackWindow = exactWindowForSample(sample.sampleIndex);
                    primeIncrementalWindowState(sample.sampleIndex, fallbackWindow);
                    break;
                }
                m_incrementalNextWindowStartSampleIndex = *nextBoundary;
            }
            if (m_incrementalWindowIndex) {
                m_lastWindowSampleIndex = sample.sampleIndex;
            }
        }

        if (!m_incrementalWindowIndex) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }
        const auto windowIndex = *m_incrementalWindowIndex;

        if (sample.absoluteFrequencyHz < m_config.sourceMinHz
            || sample.absoluteFrequencyHz > m_config.sourceMaxHz) {
            if (sample.absoluteFrequencyHz <= 0) {
                ++m_counters.invalidSamples;
                ++result.deltaCounters.invalidSamples;
            } else {
                ++m_counters.outOfRangeSamples;
                ++result.deltaCounters.outOfRangeSamples;
            }
            continue;
        }

        if (sample.amplitude < m_config.amplitudeFloor) {
            continue;
        }

        if (sample.bandIndex < 0
            || static_cast<std::size_t>(sample.bandIndex) >= m_config.bandCapacity) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }

        const auto relativeFrequencyHz =
            sample.absoluteFrequencyHz - m_config.sourceMinHz;
        const auto numerator = relativeFrequencyHz * m_fastBinMultiplier;
        const auto binIndex = static_cast<std::size_t>(
            std::clamp(static_cast<int>(numerator / m_fastBinRangeHz),
                       0,
                       m_config.renderBinCount - 1));

        if (!m_openWindow) {
            openWindow(windowIndex, sample.sampleIndex);
        } else if (m_openWindow->windowIndex != windowIndex) {
            mergeLocalAccumulationIntoOpenWindow();
            if (auto snapshot = closeOpenWindow()) {
                result.snapshots.push_back(std::move(snapshot));
                ++result.deltaCounters.producedSnapshots;
            }
            openWindow(windowIndex, sample.sampleIndex);
        }

        if (!m_localHasSamples) {
            m_localFirstSampleIndex = sample.sampleIndex;
            m_localLastSampleIndex = sample.sampleIndex;
            m_localHasSamples = true;
        } else {
            m_localLastSampleIndex = sample.sampleIndex;
        }

        const auto amplitude =
            static_cast<std::uint16_t>(std::clamp(
                sample.amplitude,
                0,
                static_cast<int>(std::numeric_limits<std::uint16_t>::max())));

        auto& bin = m_localBins[binIndex];
        if (bin.used == 0) {
            bin.used = 1;
            m_touchedBins.push_back(static_cast<std::uint32_t>(binIndex));
        }
        bin.totalPeak = std::max(bin.totalPeak, amplitude);
        if (m_config.separateBeams && sample.beamIndex == 0) {
            bin.beam0Peak = std::max(bin.beam0Peak, amplitude);
        } else if (m_config.separateBeams && sample.beamIndex == 1) {
            bin.beam1Peak = std::max(bin.beam1Peak, amplitude);
        }
        if (bin.hitCount < std::numeric_limits<std::uint16_t>::max()) {
            ++bin.hitCount;
        }

        const auto bandIndex = static_cast<std::size_t>(sample.bandIndex);
        auto& band = m_localBands[bandIndex];
        if (band.used == 0) {
            band.used = 1;
            band.bandIndex = sample.bandIndex;
            m_touchedBands.push_back(static_cast<std::uint32_t>(bandIndex));
        }
        ++band.sampleCount;
        band.totalPeak = std::max(band.totalPeak, amplitude);
        if (m_config.separateBeams && sample.beamIndex == 0) {
            band.beam0Peak = std::max(band.beam0Peak, amplitude);
        } else if (m_config.separateBeams && sample.beamIndex == 1) {
            band.beam1Peak = std::max(band.beam1Peak, amplitude);
        }
    }

    mergeLocalAccumulationIntoOpenWindow();
    result.timing.sampleLoop = Clock::now() - sampleLoopStartedAt;
    result.timing.total = Clock::now() - totalStartedAt;
    result.incrementalWindowFallbacks =
        m_incrementalWindowFallbacks - incrementalFallbacksBefore;

    return result;
}

bool SpectrumAggregator::hasFixedBandSummarySlot(int bandIndex) const noexcept
{
    if (!usesFixedBandSummaryStorage() || bandIndex < 0) {
        return false;
    }
    return static_cast<std::size_t>(bandIndex) < m_config.bandCapacity;
}

SpectrumAggregationResult SpectrumAggregator::consume(const SignalBlock& block)
{
    if (block.empty()) {
        return {};
    }

    ++m_counters.consumedBlocks;
    auto result = consume(block.samples());
    result.deltaCounters.consumedBlocks = 1;
    return result;
}

SpectrumAggregationResult SpectrumAggregator::consume(
    std::span<const core::SignalSample> samples)
{
    if (usesTrustedBlockLocalFastPath()) {
        return consumeTrustedBlockLocalFast(samples);
    }

    SpectrumAggregationResult result;
    const auto totalStartedAt = Clock::now();
    const bool detailedTiming = m_config.enableDetailedTiming;
    const bool blockLocalFastPath = usesBlockLocalFastPath();
    const auto incrementalFallbacksBefore = m_incrementalWindowFallbacks;
    result.usedFastWindowIndex = m_canUseFastWindowIndex;
    result.usedIncrementalWindowIndex = m_canUseIncrementalWindowIndex;
    result.usedFastBinIndex = m_canUseFastBinIndex;
    result.usedFastBandSummaryStorage = usesFixedBandSummaryStorage();
    result.usedBlockLocalAccumulation = blockLocalFastPath && !samples.empty();
    result.snapshots.reserve(2);
    if (blockLocalFastPath) {
        prepareLocalAccumulationBuffers();
    }

    Clock::duration sampledWindowCalculation{};
    Clock::duration sampledBinCalculation{};
    Clock::duration sampledBinUpdate{};
    Clock::duration sampledBandSummaryUpdate{};
    std::uint64_t windowCalculationCount = 0;
    std::uint64_t sampledWindowCalculationCount = 0;
    std::uint64_t binCalculationCount = 0;
    std::uint64_t sampledBinCalculationCount = 0;
    std::uint64_t binUpdateCount = 0;
    std::uint64_t sampledBinUpdateCount = 0;
    std::uint64_t bandSummaryUpdateCount = 0;
    std::uint64_t sampledBandSummaryUpdateCount = 0;

    const auto sampleLoopStartedAt = Clock::now();
    for (const auto& sample : samples) {
        ++m_counters.consumedSamples;
        ++result.deltaCounters.consumedSamples;

        if (!m_config.trustedSamples && !hasValidUntrustedSample(sample)) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }

        const bool measureWindowCalculation = detailedTiming
            && shouldMeasureSampledTiming(windowCalculationCount++);
        const auto windowCalculationStartedAt = measureWindowCalculation
            ? Clock::now()
            : Clock::time_point{};
        const auto windowIndex = windowForSample(sample.sampleIndex);
        if (measureWindowCalculation) {
            sampledWindowCalculation += Clock::now() - windowCalculationStartedAt;
            ++sampledWindowCalculationCount;
        }
        if (!windowIndex || sample.amplitude <= 0) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }

        const bool measureBinCalculation = detailedTiming
            && shouldMeasureSampledTiming(binCalculationCount++);
        const auto binCalculationStartedAt = measureBinCalculation
            ? Clock::now()
            : Clock::time_point{};
        const int binIndex = binForFrequency(sample.absoluteFrequencyHz);
        if (measureBinCalculation) {
            sampledBinCalculation += Clock::now() - binCalculationStartedAt;
            ++sampledBinCalculationCount;
        }
        if (binIndex < 0) {
            if (sample.absoluteFrequencyHz <= 0 || m_config.sourceMaxHz <= m_config.sourceMinHz) {
                ++m_counters.invalidSamples;
                ++result.deltaCounters.invalidSamples;
            } else {
                ++m_counters.outOfRangeSamples;
                ++result.deltaCounters.outOfRangeSamples;
            }
            continue;
        }

        if (sample.amplitude < m_config.amplitudeFloor) {
            continue;
        }

        if (usesFixedBandSummaryStorage()
            && !hasFixedBandSummarySlot(sample.bandIndex)) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }

        if (!m_openWindow) {
            openWindow(*windowIndex, sample.sampleIndex);
        } else if (m_openWindow->windowIndex != *windowIndex) {
            if (blockLocalFastPath) {
                mergeLocalAccumulationIntoOpenWindow();
            }
            const auto closeStartedAt = detailedTiming
                ? Clock::now()
                : Clock::time_point{};
            if (auto snapshot = closeOpenWindow(detailedTiming ? &result.timing
                                                               : nullptr)) {
                result.snapshots.push_back(std::move(snapshot));
                ++result.deltaCounters.producedSnapshots;
            }
            if (detailedTiming) {
                result.timing.closeWindow += Clock::now() - closeStartedAt;
            }
            openWindow(*windowIndex, sample.sampleIndex);
        }

        if (blockLocalFastPath) {
            updateLocalWindowBounds(sample);

            const bool measureBinUpdate = detailedTiming
                && shouldMeasureSampledTiming(binUpdateCount++);
            const auto binUpdateStartedAt = measureBinUpdate
                ? Clock::now()
                : Clock::time_point{};
            updateLocalBinForSample(sample, binIndex);
            if (measureBinUpdate) {
                sampledBinUpdate += Clock::now() - binUpdateStartedAt;
                ++sampledBinUpdateCount;
            }

            const bool measureBandSummaryUpdate = detailedTiming
                && shouldMeasureSampledTiming(bandSummaryUpdateCount++);
            const auto bandSummaryUpdateStartedAt = measureBandSummaryUpdate
                ? Clock::now()
                : Clock::time_point{};
            updateLocalBandSummaryForSample(sample);
            if (measureBandSummaryUpdate) {
                sampledBandSummaryUpdate +=
                    Clock::now() - bandSummaryUpdateStartedAt;
                ++sampledBandSummaryUpdateCount;
            }
        } else {
            updateOpenWindowBounds(sample);

            const bool measureBinUpdate = detailedTiming
                && shouldMeasureSampledTiming(binUpdateCount++);
            const auto binUpdateStartedAt = measureBinUpdate
                ? Clock::now()
                : Clock::time_point{};
            updateBinForSample(sample, binIndex);
            if (measureBinUpdate) {
                sampledBinUpdate += Clock::now() - binUpdateStartedAt;
                ++sampledBinUpdateCount;
            }

            const bool measureBandSummaryUpdate = detailedTiming
                && shouldMeasureSampledTiming(bandSummaryUpdateCount++);
            const auto bandSummaryUpdateStartedAt = measureBandSummaryUpdate
                ? Clock::now()
                : Clock::time_point{};
            updateBandSummaryForSample(sample, result.deltaCounters);
            if (measureBandSummaryUpdate) {
                sampledBandSummaryUpdate +=
                    Clock::now() - bandSummaryUpdateStartedAt;
                ++sampledBandSummaryUpdateCount;
            }
        }
    }
    if (blockLocalFastPath) {
        mergeLocalAccumulationIntoOpenWindow();
    }
    result.timing.sampleLoop = Clock::now() - sampleLoopStartedAt;
    if (detailedTiming) {
        result.timing.windowCalculation =
            scaleSampledDuration(sampledWindowCalculation,
                                 sampledWindowCalculationCount,
                                 windowCalculationCount);
        result.timing.binCalculation =
            scaleSampledDuration(sampledBinCalculation,
                                 sampledBinCalculationCount,
                                 binCalculationCount);
        result.timing.binUpdate = scaleSampledDuration(sampledBinUpdate,
                                                       sampledBinUpdateCount,
                                                       binUpdateCount);
        result.timing.bandSummaryUpdate =
            scaleSampledDuration(sampledBandSummaryUpdate,
                                 sampledBandSummaryUpdateCount,
                                 bandSummaryUpdateCount);
        result.timing.windowCalculation =
            std::min(result.timing.windowCalculation, result.timing.sampleLoop);
        result.timing.binCalculation =
            std::min(result.timing.binCalculation, result.timing.sampleLoop);
        result.timing.binUpdate =
            std::min(result.timing.binUpdate, result.timing.sampleLoop);
        result.timing.bandSummaryUpdate =
            std::min(result.timing.bandSummaryUpdate, result.timing.sampleLoop);
    }
    result.timing.total = Clock::now() - totalStartedAt;
    result.incrementalWindowFallbacks =
        m_incrementalWindowFallbacks - incrementalFallbacksBefore;

    return result;
}

SpectrumAggregationResult SpectrumAggregator::flush()
{
    SpectrumAggregationResult result;
    const auto totalStartedAt = Clock::now();
    const bool detailedTiming = m_config.enableDetailedTiming;
    result.usedFastWindowIndex = m_canUseFastWindowIndex;
    result.usedIncrementalWindowIndex = m_canUseIncrementalWindowIndex;
    result.usedFastBinIndex = m_canUseFastBinIndex;
    result.usedFastBandSummaryStorage = usesFixedBandSummaryStorage();
    if (usesBlockLocalFastPath()) {
        mergeLocalAccumulationIntoOpenWindow();
    }
    const auto closeStartedAt = detailedTiming ? Clock::now() : Clock::time_point{};
    if (auto snapshot = closeOpenWindow(detailedTiming ? &result.timing : nullptr)) {
        result.snapshots.push_back(std::move(snapshot));
        ++result.deltaCounters.producedSnapshots;
    }
    if (detailedTiming) {
        result.timing.closeWindow = Clock::now() - closeStartedAt;
    }
    result.timing.total = Clock::now() - totalStartedAt;
    return result;
}

std::optional<std::uint64_t> SpectrumAggregator::windowForSample(
    std::uint64_t sampleIndex)
{
    if (m_config.snapshotPeriodNs == 0 || m_config.timeBase.samplePeriodNs == 0) {
        return std::nullopt;
    }
    if (sampleIndex < m_config.timeBase.firstSampleIndex) {
        return std::nullopt;
    }

    if (!m_config.useFastWindowIndex
        || m_config.windowIndexMode == SpectrumWindowIndexMode::ExactInt128) {
        return exactWindowForSample(sampleIndex);
    }

    if (m_config.windowIndexMode == SpectrumWindowIndexMode::DivisibleSamplePeriod) {
        if (m_canUseFastWindowIndex) {
            return (sampleIndex - m_config.timeBase.firstSampleIndex)
                / m_samplesPerWindow;
        }
        return exactWindowForSample(sampleIndex);
    }

    if (m_config.windowIndexMode == SpectrumWindowIndexMode::IncrementalMonotonic) {
        return incrementalWindowForSample(sampleIndex);
    }

    return exactWindowForSample(sampleIndex);
}

std::optional<std::uint64_t> SpectrumAggregator::exactWindowForSample(
    std::uint64_t sampleIndex) const
{
    if (m_config.snapshotPeriodNs == 0 || m_config.timeBase.samplePeriodNs == 0) {
        return std::nullopt;
    }
    if (sampleIndex < m_config.timeBase.firstSampleIndex) {
        return std::nullopt;
    }

    const auto relativeSampleIndex = sampleIndex - m_config.timeBase.firstSampleIndex;
    const auto relativeNs =
        static_cast<unsigned __int128>(relativeSampleIndex)
        * static_cast<unsigned __int128>(m_config.timeBase.samplePeriodNs);
    const auto window =
        relativeNs / static_cast<unsigned __int128>(m_config.snapshotPeriodNs);
    if (window > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(window);
}

std::optional<std::uint64_t> SpectrumAggregator::firstSampleIndexForWindow(
    std::uint64_t windowIndex) const
{
    if (m_config.snapshotPeriodNs == 0 || m_config.timeBase.samplePeriodNs == 0) {
        return std::nullopt;
    }

    const auto numerator =
        static_cast<unsigned __int128>(windowIndex)
        * static_cast<unsigned __int128>(m_config.snapshotPeriodNs);
    const auto denominator =
        static_cast<unsigned __int128>(m_config.timeBase.samplePeriodNs);
    auto relativeSampleIndex = numerator / denominator;
    if (numerator % denominator != 0) {
        ++relativeSampleIndex;
    }
    if (relativeSampleIndex > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }

    const auto relative = static_cast<std::uint64_t>(relativeSampleIndex);
    if (relative
        > std::numeric_limits<std::uint64_t>::max()
            - m_config.timeBase.firstSampleIndex) {
        return std::nullopt;
    }
    return m_config.timeBase.firstSampleIndex + relative;
}

std::optional<std::uint64_t> SpectrumAggregator::incrementalWindowForSample(
    std::uint64_t sampleIndex)
{
    if (!m_canUseIncrementalWindowIndex) {
        return exactWindowForSample(sampleIndex);
    }

    if (m_lastWindowSampleIndex && sampleIndex < *m_lastWindowSampleIndex) {
        ++m_incrementalWindowFallbacks;
        resetIncrementalWindowState();
        const auto exact = exactWindowForSample(sampleIndex);
        primeIncrementalWindowState(sampleIndex, exact);
        return exact;
    }

    if (!m_incrementalWindowIndex
        || !m_incrementalNextWindowStartSampleIndex) {
        const auto exact = exactWindowForSample(sampleIndex);
        primeIncrementalWindowState(sampleIndex, exact);
        return exact;
    }

    while (m_incrementalNextWindowStartSampleIndex
           && sampleIndex >= *m_incrementalNextWindowStartSampleIndex) {
        if (*m_incrementalWindowIndex
            == std::numeric_limits<std::uint64_t>::max()) {
            ++m_incrementalWindowFallbacks;
            const auto exact = exactWindowForSample(sampleIndex);
            primeIncrementalWindowState(sampleIndex, exact);
            return exact;
        }

        ++(*m_incrementalWindowIndex);
        if (*m_incrementalWindowIndex
            == std::numeric_limits<std::uint64_t>::max()) {
            m_incrementalNextWindowStartSampleIndex.reset();
            continue;
        }

        const auto nextBoundary =
            firstSampleIndexForWindow(*m_incrementalWindowIndex + 1);
        if (!nextBoundary) {
            ++m_incrementalWindowFallbacks;
            const auto exact = exactWindowForSample(sampleIndex);
            primeIncrementalWindowState(sampleIndex, exact);
            return exact;
        }
        m_incrementalNextWindowStartSampleIndex = *nextBoundary;
    }

    m_lastWindowSampleIndex = sampleIndex;
    return m_incrementalWindowIndex;
}

void SpectrumAggregator::resetIncrementalWindowState() noexcept
{
    m_incrementalWindowIndex.reset();
    m_incrementalNextWindowStartSampleIndex.reset();
    m_lastWindowSampleIndex.reset();
}

void SpectrumAggregator::primeIncrementalWindowState(
    std::uint64_t sampleIndex,
    std::optional<std::uint64_t> windowIndex)
{
    if (!windowIndex) {
        resetIncrementalWindowState();
        return;
    }

    m_incrementalWindowIndex = *windowIndex;
    if (*windowIndex == std::numeric_limits<std::uint64_t>::max()) {
        m_incrementalNextWindowStartSampleIndex.reset();
    } else {
        m_incrementalNextWindowStartSampleIndex =
            firstSampleIndexForWindow(*windowIndex + 1);
    }
    m_lastWindowSampleIndex = sampleIndex;
}

int SpectrumAggregator::binForFrequency(std::int64_t frequencyHz) const noexcept
{
    if (m_config.renderBinCount <= 0 || m_config.sourceMaxHz <= m_config.sourceMinHz
        || frequencyHz < m_config.sourceMinHz || frequencyHz > m_config.sourceMaxHz) {
        return -1;
    }
    if (m_config.renderBinCount == 1) {
        return 0;
    }

    if (m_canUseFastBinIndex) {
        const auto relativeFrequencyHz = frequencyHz - m_config.sourceMinHz;
        const auto numerator = relativeFrequencyHz * m_fastBinMultiplier;
        const auto bin = numerator / m_fastBinRangeHz;
        return std::clamp(static_cast<int>(bin), 0, m_config.renderBinCount - 1);
    }

    const auto numerator =
        static_cast<__int128>(frequencyHz - m_config.sourceMinHz)
        * static_cast<__int128>(m_config.renderBinCount - 1);
    const auto denominator =
        static_cast<__int128>(m_config.sourceMaxHz - m_config.sourceMinHz);
    const auto bin = denominator == 0 ? 0 : numerator / denominator;
    return std::clamp(static_cast<int>(bin), 0, m_config.renderBinCount - 1);
}

std::uint16_t SpectrumAggregator::clampAmplitude(int amplitude) noexcept
{
    return static_cast<std::uint16_t>(
        std::clamp(amplitude,
                   0,
                   static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
}

void SpectrumAggregator::incrementHitCount(SpectrumBin& bin) noexcept
{
    if (bin.hitCount < std::numeric_limits<std::uint16_t>::max()) {
        ++bin.hitCount;
    }
}

std::uint16_t SpectrumAggregator::saturatedAddHitCount(
    std::uint16_t left,
    std::uint16_t right) noexcept
{
    const auto max = std::numeric_limits<std::uint16_t>::max();
    if (left > max - right) {
        return max;
    }
    return static_cast<std::uint16_t>(left + right);
}

void SpectrumAggregator::openWindow(std::uint64_t windowIndex,
                                    std::uint64_t sampleIndex)
{
    OpenWindow window;
    window.windowIndex = windowIndex;
    window.firstSampleIndex = sampleIndex;
    window.lastSampleIndex = sampleIndex;
    window.bins.resize(static_cast<std::size_t>(m_config.renderBinCount));
    std::fill(window.bins.begin(), window.bins.end(), SpectrumBin{});
    if (usesFixedBandSummaryStorage()) {
        window.bandSummaryVector.resize(m_config.bandCapacity);
        window.bandSummaryUsed.assign(m_config.bandCapacity, std::uint8_t{0});
    }
    m_openWindow = std::move(window);
}

std::shared_ptr<const SpectrumSnapshot> SpectrumAggregator::closeOpenWindow(
    SpectrumAggregatorTiming* timing)
{
    if (!m_openWindow || !m_openWindow->hasSamples) {
        m_openWindow.reset();
        return {};
    }

    const auto snapshotBuildStartedAt = Clock::now();
    auto snapshot = std::make_shared<SpectrumSnapshot>();
    snapshot->sequenceId = m_nextSnapshotSequenceId++;
    snapshot->createdUtcNs = currentSpectrumSnapshotUtcNs();
    snapshot->sourceMinHz = m_config.sourceMinHz;
    snapshot->sourceMaxHz = m_config.sourceMaxHz;
    snapshot->renderBinCount = m_config.renderBinCount;
    snapshot->snapshotPeriodNs = m_config.snapshotPeriodNs;
    snapshot->firstSampleIndex = m_openWindow->firstSampleIndex;
    snapshot->lastSampleIndex = m_openWindow->lastSampleIndex;
    snapshot->bins = std::move(m_openWindow->bins);
    if (usesFixedBandSummaryStorage()) {
        snapshot->bandSummaries.reserve(m_openWindow->usedBandSummaryCount);
        const auto count = std::min(m_openWindow->bandSummaryVector.size(),
                                    m_openWindow->bandSummaryUsed.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (m_openWindow->bandSummaryUsed[index] == 0) {
                continue;
            }
            snapshot->bandSummaries.push_back(m_openWindow->bandSummaryVector[index]);
        }
    } else {
        snapshot->bandSummaries = std::move(m_openWindow->bandSummaries);
    }
    snapshot->counters.producedSnapshots = m_counters.producedSnapshots + 1;
    snapshot->counters.invalidSamples = m_counters.invalidSamples;
    snapshot->counters.outOfRangeSamples = m_counters.outOfRangeSamples;

    ++m_counters.producedSnapshots;
    if (timing) {
        timing->snapshotBuild += Clock::now() - snapshotBuildStartedAt;
    }
    m_openWindow.reset();
    return snapshot;
}

void SpectrumAggregator::updateOpenWindowBounds(const core::SignalSample& sample)
{
    if (!m_openWindow) {
        return;
    }

    m_openWindow->firstSampleIndex =
        std::min(m_openWindow->firstSampleIndex, sample.sampleIndex);
    m_openWindow->lastSampleIndex =
        std::max(m_openWindow->lastSampleIndex, sample.sampleIndex);
    m_openWindow->hasSamples = true;
}

void SpectrumAggregator::updateBinForSample(const core::SignalSample& sample,
                                            int binIndex)
{
    if (!m_openWindow || binIndex < 0
        || static_cast<std::size_t>(binIndex) >= m_openWindow->bins.size()) {
        return;
    }

    const auto amplitude = clampAmplitude(sample.amplitude);
    auto& bin = m_openWindow->bins[static_cast<std::size_t>(binIndex)];
    bin.totalPeak = std::max(bin.totalPeak, amplitude);
    if (m_config.separateBeams && sample.beamIndex == 0) {
        bin.beam0Peak = std::max(bin.beam0Peak, amplitude);
    } else if (m_config.separateBeams && sample.beamIndex == 1) {
        bin.beam1Peak = std::max(bin.beam1Peak, amplitude);
    }
    incrementHitCount(bin);
}

void SpectrumAggregator::updateBandSummaryForSample(
    const core::SignalSample& sample,
    SpectrumAggregatorCounters& deltaCounters)
{
    if (!m_openWindow) {
        return;
    }

    auto* summary = bandSummaryForSample(sample.bandIndex);
    if (!summary) {
        ++m_counters.invalidSamples;
        ++deltaCounters.invalidSamples;
        return;
    }

    const auto amplitude = clampAmplitude(sample.amplitude);
    ++summary->sampleCount;
    summary->totalPeak = std::max(summary->totalPeak, amplitude);
    if (m_config.separateBeams && sample.beamIndex == 0) {
        summary->beam0Peak = std::max(summary->beam0Peak, amplitude);
    } else if (m_config.separateBeams && sample.beamIndex == 1) {
        summary->beam1Peak = std::max(summary->beam1Peak, amplitude);
    }
}

SpectrumBandSummary* SpectrumAggregator::bandSummaryForSample(int bandIndex)
{
    if (!m_openWindow) {
        return nullptr;
    }

    if (usesFixedBandSummaryStorage()) {
        if (!hasFixedBandSummarySlot(bandIndex)) {
            return nullptr;
        }

        const auto band = static_cast<std::size_t>(bandIndex);
        if (band >= m_openWindow->bandSummaryVector.size()
            || band >= m_openWindow->bandSummaryUsed.size()) {
            return nullptr;
        }

        if (m_openWindow->bandSummaryUsed[band] == 0) {
            m_openWindow->bandSummaryUsed[band] = 1;
            ++m_openWindow->usedBandSummaryCount;
            auto& summary = m_openWindow->bandSummaryVector[band];
            summary = SpectrumBandSummary{};
            summary.bandIndex = bandIndex;
        }
        return &m_openWindow->bandSummaryVector[band];
    }

    auto found = std::find_if(m_openWindow->bandSummaries.begin(),
                              m_openWindow->bandSummaries.end(),
                              [bandIndex](const auto& summary) {
                                  return summary.bandIndex == bandIndex;
                              });
    if (found != m_openWindow->bandSummaries.end()) {
        return &*found;
    }

    m_openWindow->bandSummaries.push_back(SpectrumBandSummary{});
    auto& summary = m_openWindow->bandSummaries.back();
    summary.bandIndex = bandIndex;
    return &summary;
}

void SpectrumAggregator::prepareLocalAccumulationBuffers()
{
    const auto binCount = static_cast<std::size_t>(m_config.renderBinCount);
    if (m_localBins.size() != binCount) {
        m_localBins.assign(binCount, LocalBinAccumulator{});
    }
    if (m_touchedBins.capacity() < binCount) {
        m_touchedBins.reserve(binCount);
    }

    if (m_localBands.size() != m_config.bandCapacity) {
        m_localBands.assign(m_config.bandCapacity, LocalBandAccumulator{});
    }
    if (m_touchedBands.capacity() < m_config.bandCapacity) {
        m_touchedBands.reserve(m_config.bandCapacity);
    }

    resetLocalAccumulationTouched();
}

void SpectrumAggregator::resetLocalAccumulationTouched()
{
    for (const auto binIndex : m_touchedBins) {
        const auto index = static_cast<std::size_t>(binIndex);
        if (index < m_localBins.size()) {
            m_localBins[index] = LocalBinAccumulator{};
        }
    }
    m_touchedBins.clear();

    for (const auto bandIndex : m_touchedBands) {
        const auto index = static_cast<std::size_t>(bandIndex);
        if (index < m_localBands.size()) {
            m_localBands[index] = LocalBandAccumulator{};
        }
    }
    m_touchedBands.clear();

    m_localHasSamples = false;
    m_localFirstSampleIndex = 0;
    m_localLastSampleIndex = 0;
}

void SpectrumAggregator::updateLocalWindowBounds(
    const core::SignalSample& sample)
{
    if (!m_localHasSamples) {
        m_localFirstSampleIndex = sample.sampleIndex;
        m_localLastSampleIndex = sample.sampleIndex;
        m_localHasSamples = true;
        return;
    }

    m_localFirstSampleIndex =
        std::min(m_localFirstSampleIndex, sample.sampleIndex);
    m_localLastSampleIndex =
        std::max(m_localLastSampleIndex, sample.sampleIndex);
}

void SpectrumAggregator::updateLocalBinForSample(
    const core::SignalSample& sample,
    int binIndex)
{
    if (binIndex < 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(binIndex);
    if (index >= m_localBins.size()) {
        return;
    }

    auto& bin = m_localBins[index];
    if (bin.used == 0) {
        bin.used = 1;
        m_touchedBins.push_back(static_cast<std::uint32_t>(index));
    }

    const auto amplitude = clampAmplitude(sample.amplitude);
    bin.totalPeak = std::max(bin.totalPeak, amplitude);
    if (m_config.separateBeams && sample.beamIndex == 0) {
        bin.beam0Peak = std::max(bin.beam0Peak, amplitude);
    } else if (m_config.separateBeams && sample.beamIndex == 1) {
        bin.beam1Peak = std::max(bin.beam1Peak, amplitude);
    }
    if (bin.hitCount < std::numeric_limits<std::uint16_t>::max()) {
        ++bin.hitCount;
    }
}

void SpectrumAggregator::updateLocalBandSummaryForSample(
    const core::SignalSample& sample)
{
    if (!hasFixedBandSummarySlot(sample.bandIndex)) {
        return;
    }

    const auto index = static_cast<std::size_t>(sample.bandIndex);
    if (index >= m_localBands.size()) {
        return;
    }

    auto& band = m_localBands[index];
    if (band.used == 0) {
        band.used = 1;
        band.bandIndex = sample.bandIndex;
        m_touchedBands.push_back(static_cast<std::uint32_t>(index));
    }

    const auto amplitude = clampAmplitude(sample.amplitude);
    ++band.sampleCount;
    band.totalPeak = std::max(band.totalPeak, amplitude);
    if (m_config.separateBeams && sample.beamIndex == 0) {
        band.beam0Peak = std::max(band.beam0Peak, amplitude);
    } else if (m_config.separateBeams && sample.beamIndex == 1) {
        band.beam1Peak = std::max(band.beam1Peak, amplitude);
    }
}

void SpectrumAggregator::mergeLocalAccumulationIntoOpenWindow()
{
    if (!m_openWindow) {
        resetLocalAccumulationTouched();
        return;
    }

    for (const auto binIndex : m_touchedBins) {
        const auto index = static_cast<std::size_t>(binIndex);
        if (index >= m_localBins.size() || index >= m_openWindow->bins.size()) {
            continue;
        }

        const auto& local = m_localBins[index];
        if (local.used == 0) {
            continue;
        }

        auto& dst = m_openWindow->bins[index];
        dst.totalPeak = std::max(dst.totalPeak, local.totalPeak);
        dst.beam0Peak = std::max(dst.beam0Peak, local.beam0Peak);
        dst.beam1Peak = std::max(dst.beam1Peak, local.beam1Peak);
        dst.hitCount = saturatedAddHitCount(dst.hitCount, local.hitCount);
    }

    for (const auto bandIndex : m_touchedBands) {
        const auto index = static_cast<std::size_t>(bandIndex);
        if (index >= m_localBands.size()
            || index >= m_openWindow->bandSummaryVector.size()
            || index >= m_openWindow->bandSummaryUsed.size()) {
            continue;
        }

        const auto& local = m_localBands[index];
        if (local.used == 0) {
            continue;
        }

        if (m_openWindow->bandSummaryUsed[index] == 0) {
            m_openWindow->bandSummaryUsed[index] = 1;
            ++m_openWindow->usedBandSummaryCount;
            auto& summary = m_openWindow->bandSummaryVector[index];
            summary = SpectrumBandSummary{};
            summary.bandIndex = local.bandIndex;
        }

        auto& dst = m_openWindow->bandSummaryVector[index];
        dst.sampleCount += local.sampleCount;
        dst.totalPeak = std::max(dst.totalPeak, local.totalPeak);
        dst.beam0Peak = std::max(dst.beam0Peak, local.beam0Peak);
        dst.beam1Peak = std::max(dst.beam1Peak, local.beam1Peak);
    }

    if (m_localHasSamples) {
        m_openWindow->firstSampleIndex =
            std::min(m_openWindow->firstSampleIndex, m_localFirstSampleIndex);
        m_openWindow->lastSampleIndex =
            std::max(m_openWindow->lastSampleIndex, m_localLastSampleIndex);
        m_openWindow->hasSamples = true;
    }

    resetLocalAccumulationTouched();
}

} // namespace siriusscope::pipeline
