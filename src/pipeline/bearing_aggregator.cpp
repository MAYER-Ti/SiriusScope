#include "pipeline/bearing_aggregator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace siriusscope::pipeline {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxFlatCandidates = 1'000'000;
constexpr std::uint64_t kTimingSampleStride = 64;

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double beamDifference(std::uint16_t beam0Peak, std::uint16_t beam1Peak)
{
    const auto sum = static_cast<int>(beam0Peak) + static_cast<int>(beam1Peak);
    if (sum <= 0) {
        return 0.0;
    }

    return (static_cast<double>(beam0Peak) - static_cast<double>(beam1Peak))
        / static_cast<double>(sum);
}

double candidateQuality(std::uint16_t beam0Peak, std::uint16_t beam1Peak)
{
    const auto maxAmplitude = std::max(beam0Peak, beam1Peak);
    const auto amplitudeNorm = clamp01(
        static_cast<double>(maxAmplitude - core::DomainConstraints::minAmplitude)
        / static_cast<double>(core::DomainConstraints::maxAmplitude
                              - core::DomainConstraints::minAmplitude));
    const auto beamSumNorm = clamp01(
        static_cast<double>(beam0Peak + beam1Peak)
        / static_cast<double>(core::DomainConstraints::maxAmplitude
                              * core::DomainConstraints::currentBeamCount));

    return 0.7 * amplitudeNorm + 0.3 * beamSumNorm;
}

BearingAggregatorConfig normalizeConfig(BearingAggregatorConfig config)
{
    config.frequencyBinCount = std::max(1, config.frequencyBinCount);

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

bool BearingAggregator::CandidateKey::operator<(
    const CandidateKey& other) const noexcept
{
    if (bandIndex != other.bandIndex) {
        return bandIndex < other.bandIndex;
    }
    return frequencyBin < other.frequencyBin;
}

BearingAggregator::BearingAggregator(BearingAggregatorConfig config)
    : m_config(normalizeConfig(std::move(config)))
{
    prepareDerivedConfig();
}

void BearingAggregator::reset()
{
    m_counters = {};
    m_openWindow.reset();
    m_nextSnapshotSequenceId = 1;
}

void BearingAggregator::setConfig(BearingAggregatorConfig config)
{
    m_config = normalizeConfig(std::move(config));
    prepareDerivedConfig();
    reset();
}

void BearingAggregator::prepareDerivedConfig()
{
    m_effectiveCandidateStorageMode = m_config.candidateStorageMode;
    if (m_config.candidateStorageMode == BearingCandidateStorageMode::FlatBandBinVector) {
        const auto frequencyBinCount =
            static_cast<std::size_t>(m_config.frequencyBinCount);
        const bool capacityOverflows =
            frequencyBinCount == 0
            || m_config.bandCapacity
                > std::numeric_limits<std::size_t>::max() / frequencyBinCount;
        const auto flatCandidateCount = capacityOverflows
            ? kMaxFlatCandidates + 1
            : m_config.bandCapacity * frequencyBinCount;
        if (capacityOverflows || flatCandidateCount > kMaxFlatCandidates) {
            m_effectiveCandidateStorageMode =
                BearingCandidateStorageMode::MapByBandAndBin;
        }
    }

    m_samplesPerWindow = 0;
    m_useFastWindowIndex = false;
    if (m_config.timeBase.samplePeriodNs > 0 && m_config.windowPeriodNs > 0
        && m_config.windowPeriodNs % m_config.timeBase.samplePeriodNs == 0) {
        m_samplesPerWindow =
            m_config.windowPeriodNs / m_config.timeBase.samplePeriodNs;
        m_useFastWindowIndex = m_samplesPerWindow > 0;
    }

    m_fastBinRangeHz = 0;
    m_fastBinMultiplier = 0;
    m_useFastBinIndex = false;
    if (m_config.frequencyBinCount > 1
        && m_config.sourceMaxHz > m_config.sourceMinHz) {
        const auto range = m_config.sourceMaxHz - m_config.sourceMinHz;
        const auto multiplier =
            static_cast<std::int64_t>(m_config.frequencyBinCount - 1);
        if (range > 0 && multiplier > 0
            && range <= std::numeric_limits<std::int64_t>::max() / multiplier) {
            m_fastBinRangeHz = range;
            m_fastBinMultiplier = multiplier;
            m_useFastBinIndex = true;
        }
    }
}

bool BearingAggregator::usesFlatCandidateStorage() const noexcept
{
    return m_effectiveCandidateStorageMode
        == BearingCandidateStorageMode::FlatBandBinVector;
}

bool BearingAggregator::hasValidUntrustedSample(
    const core::SignalSample& sample) const
{
    return core::validateAmplitude(sample.amplitude).isValid()
        && core::validateBandIndex(sample.bandIndex).isValid()
        && core::validateBeamIndex(sample.beamIndex).isValid()
        && core::validateSystemFrequency(sample.absoluteFrequencyHz).isValid();
}

BearingAggregationResult BearingAggregator::consume(const SignalBlock& block)
{
    if (block.empty()) {
        return {};
    }

    ++m_counters.consumedBlocks;
    auto result = consumeSamples(block.samples(), block.antennaAzimuthDeg());
    result.deltaCounters.consumedBlocks = 1;
    return result;
}

BearingAggregationResult BearingAggregator::consume(
    std::span<const core::SignalSample> samples)
{
    return consumeSamples(samples, std::nullopt);
}

BearingAggregationResult BearingAggregator::consumeSamples(
    std::span<const core::SignalSample> samples,
    std::optional<double> antennaAzimuthDeg)
{
    BearingAggregationResult result;
    const auto totalStartedAt = Clock::now();
    result.usedFastCandidateStorage = usesFlatCandidateStorage();
    result.snapshots.reserve(2);

    const double blockAntennaAzimuthDeg =
        antennaAzimuthDeg.value_or(m_config.fallbackAntennaAzimuthDeg);

    Clock::duration sampledWindowCalculation{};
    Clock::duration sampledBinCalculation{};
    Clock::duration sampledCandidateUpdate{};
    std::uint64_t windowCalculationCount = 0;
    std::uint64_t sampledWindowCalculationCount = 0;
    std::uint64_t binCalculationCount = 0;
    std::uint64_t sampledBinCalculationCount = 0;
    std::uint64_t candidateUpdateCount = 0;
    std::uint64_t sampledCandidateUpdateCount = 0;

    const auto sampleLoopStartedAt = Clock::now();
    for (const auto& sample : samples) {
        ++m_counters.consumedSamples;
        ++result.deltaCounters.consumedSamples;

        if (!m_config.trustedSamples && !hasValidUntrustedSample(sample)) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }

        const bool measureWindowCalculation =
            shouldMeasureSampledTiming(windowCalculationCount++);
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

        if (sample.amplitude < m_config.amplitudeFloor) {
            continue;
        }

        const bool measureBinCalculation =
            shouldMeasureSampledTiming(binCalculationCount++);
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

        if (!m_openWindow) {
            openWindow(*windowIndex, sample.sampleIndex, blockAntennaAzimuthDeg);
        } else if (m_openWindow->windowIndex != *windowIndex) {
            const auto closeStartedAt = Clock::now();
            if (auto snapshot = closeOpenWindow(result.deltaCounters, &result.timing)) {
                result.snapshots.push_back(std::move(snapshot));
            }
            result.timing.closeWindow += Clock::now() - closeStartedAt;
            openWindow(*windowIndex, sample.sampleIndex, blockAntennaAzimuthDeg);
        }

        const bool measureCandidateUpdate =
            shouldMeasureSampledTiming(candidateUpdateCount++);
        const auto candidateUpdateStartedAt = measureCandidateUpdate
            ? Clock::now()
            : Clock::time_point{};
        addSampleToOpenWindow(sample, binIndex, result.deltaCounters);
        if (measureCandidateUpdate) {
            sampledCandidateUpdate += Clock::now() - candidateUpdateStartedAt;
            ++sampledCandidateUpdateCount;
        }
    }
    result.timing.sampleLoop = Clock::now() - sampleLoopStartedAt;
    result.timing.windowCalculation += scaleSampledDuration(sampledWindowCalculation,
                                                            sampledWindowCalculationCount,
                                                            windowCalculationCount);
    result.timing.binCalculation += scaleSampledDuration(sampledBinCalculation,
                                                         sampledBinCalculationCount,
                                                         binCalculationCount);
    result.timing.candidateUpdate += scaleSampledDuration(sampledCandidateUpdate,
                                                          sampledCandidateUpdateCount,
                                                          candidateUpdateCount);
    result.timing.windowCalculation =
        std::min(result.timing.windowCalculation, result.timing.sampleLoop);
    result.timing.binCalculation =
        std::min(result.timing.binCalculation, result.timing.sampleLoop);
    result.timing.candidateUpdate =
        std::min(result.timing.candidateUpdate, result.timing.sampleLoop);
    result.timing.total = Clock::now() - totalStartedAt;

    return result;
}

BearingAggregationResult BearingAggregator::flush()
{
    BearingAggregationResult result;
    const auto totalStartedAt = Clock::now();
    result.usedFastCandidateStorage = usesFlatCandidateStorage();
    const auto closeStartedAt = Clock::now();
    if (auto snapshot = closeOpenWindow(result.deltaCounters, &result.timing)) {
        result.snapshots.push_back(std::move(snapshot));
    }
    result.timing.closeWindow = Clock::now() - closeStartedAt;
    result.timing.total = Clock::now() - totalStartedAt;
    return result;
}

std::optional<std::uint64_t> BearingAggregator::windowForSample(
    std::uint64_t sampleIndex) const
{
    if (m_config.windowPeriodNs == 0 || m_config.timeBase.samplePeriodNs == 0) {
        return std::nullopt;
    }
    if (sampleIndex < m_config.timeBase.firstSampleIndex) {
        return std::nullopt;
    }

    const auto relativeSampleIndex = sampleIndex - m_config.timeBase.firstSampleIndex;
    if (m_useFastWindowIndex) {
        return relativeSampleIndex / m_samplesPerWindow;
    }

    const auto relativeNs =
        static_cast<unsigned __int128>(relativeSampleIndex)
        * static_cast<unsigned __int128>(m_config.timeBase.samplePeriodNs);
    const auto window =
        relativeNs / static_cast<unsigned __int128>(m_config.windowPeriodNs);
    if (window > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(window);
}

std::int64_t BearingAggregator::utcNsForSample(std::uint64_t sampleIndex) const
{
    const auto converted = m_config.timeBase.globalTimeUtcNsForSample(sampleIndex);
    if (converted) {
        return *converted.value();
    }
    return m_config.timeBase.recordingStartUtcNs;
}

int BearingAggregator::binForFrequency(std::int64_t frequencyHz) const noexcept
{
    if (m_config.frequencyBinCount <= 0 || m_config.sourceMaxHz <= m_config.sourceMinHz
        || frequencyHz < m_config.sourceMinHz || frequencyHz > m_config.sourceMaxHz) {
        return -1;
    }
    if (m_config.frequencyBinCount == 1) {
        return 0;
    }

    if (m_useFastBinIndex) {
        const auto relativeFrequencyHz = frequencyHz - m_config.sourceMinHz;
        const auto numerator = relativeFrequencyHz * m_fastBinMultiplier;
        const auto bin = numerator / m_fastBinRangeHz;
        return std::clamp(static_cast<int>(bin), 0, m_config.frequencyBinCount - 1);
    }

    const auto numerator =
        static_cast<__int128>(frequencyHz - m_config.sourceMinHz)
        * static_cast<__int128>(m_config.frequencyBinCount - 1);
    const auto denominator =
        static_cast<__int128>(m_config.sourceMaxHz - m_config.sourceMinHz);
    const auto bin = denominator == 0 ? 0 : numerator / denominator;
    return std::clamp(static_cast<int>(bin), 0, m_config.frequencyBinCount - 1);
}

std::int64_t BearingAggregator::centerFrequencyForBin(std::uint32_t bin) const noexcept
{
    if (m_config.frequencyBinCount <= 1 || m_config.sourceMaxHz <= m_config.sourceMinHz) {
        return m_config.sourceMinHz;
    }

    const auto range = m_config.sourceMaxHz - m_config.sourceMinHz;
    const auto offset =
        (static_cast<__int128>(range) * static_cast<__int128>(bin)
         + static_cast<__int128>(range / 2))
        / static_cast<__int128>(m_config.frequencyBinCount - 1);
    return m_config.sourceMinHz + static_cast<std::int64_t>(offset);
}

double BearingAggregator::normalizeAzimuthDeg(double value) noexcept
{
    if (!std::isfinite(value)) {
        return 0.0;
    }

    auto normalized = std::fmod(value, core::DomainConstraints::maxAzimuthDeg);
    if (normalized < 0.0) {
        normalized += core::DomainConstraints::maxAzimuthDeg;
    }
    if (normalized >= core::DomainConstraints::maxAzimuthDeg) {
        normalized = 0.0;
    }
    return normalized;
}

std::uint16_t BearingAggregator::clampAmplitude(int amplitude) noexcept
{
    return static_cast<std::uint16_t>(
        std::clamp(amplitude,
                   0,
                   static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
}

void BearingAggregator::openWindow(std::uint64_t windowIndex,
                                   std::uint64_t sampleIndex,
                                   double antennaAzimuthDeg)
{
    OpenWindow window;
    window.windowIndex = windowIndex;
    window.firstSampleIndex = sampleIndex;
    window.lastSampleIndex = sampleIndex;
    window.antennaAzimuthDeg = normalizeAzimuthDeg(antennaAzimuthDeg);
    if (usesFlatCandidateStorage()) {
        const auto candidateCount =
            m_config.bandCapacity
            * static_cast<std::size_t>(m_config.frequencyBinCount);
        window.candidateVector.resize(candidateCount);
        window.candidateUsed.assign(candidateCount, std::uint8_t{0});
    }
    m_openWindow = std::move(window);
}

BearingCandidateAggregate* BearingAggregator::candidateForSample(int bandIndex,
                                                                 int binIndex)
{
    if (!m_openWindow || binIndex < 0) {
        return nullptr;
    }

    if (!usesFlatCandidateStorage()) {
        CandidateKey key{bandIndex, static_cast<std::uint32_t>(binIndex)};
        return &m_openWindow->candidateMap[key];
    }

    if (bandIndex < 0 || binIndex >= m_config.frequencyBinCount) {
        return nullptr;
    }
    const auto band = static_cast<std::size_t>(bandIndex);
    if (band >= m_config.bandCapacity) {
        return nullptr;
    }
    const auto bin = static_cast<std::size_t>(binIndex);
    const auto frequencyBinCount = static_cast<std::size_t>(m_config.frequencyBinCount);
    const auto index = band * frequencyBinCount + bin;
    if (index >= m_openWindow->candidateVector.size()
        || index >= m_openWindow->candidateUsed.size()) {
        return nullptr;
    }

    if (m_openWindow->candidateUsed[index] == 0) {
        m_openWindow->candidateUsed[index] = 1;
        ++m_openWindow->usedCandidateCount;
    }
    return &m_openWindow->candidateVector[index];
}

void BearingAggregator::addSampleToOpenWindow(
    const core::SignalSample& sample,
    int binIndex,
    BearingAggregatorCounters& deltaCounters)
{
    if (!m_openWindow || binIndex < 0) {
        return;
    }

    auto* candidate = candidateForSample(sample.bandIndex, binIndex);
    if (!candidate) {
        ++m_counters.invalidSamples;
        ++deltaCounters.invalidSamples;
        return;
    }

    m_openWindow->firstSampleIndex =
        std::min(m_openWindow->firstSampleIndex, sample.sampleIndex);
    m_openWindow->lastSampleIndex =
        std::max(m_openWindow->lastSampleIndex, sample.sampleIndex);

    if (!candidate->hasSamples) {
        candidate->bandIndex = sample.bandIndex;
        candidate->frequencyBin = static_cast<std::uint32_t>(binIndex);
        candidate->centerFrequencyHz = centerFrequencyForBin(candidate->frequencyBin);
        candidate->antennaAzimuthDeg = m_openWindow->antennaAzimuthDeg;
        candidate->firstSampleIndex = sample.sampleIndex;
        candidate->lastSampleIndex = sample.sampleIndex;
        candidate->hasSamples = true;
    } else {
        candidate->firstSampleIndex =
            std::min(candidate->firstSampleIndex, sample.sampleIndex);
        candidate->lastSampleIndex =
            std::max(candidate->lastSampleIndex, sample.sampleIndex);
    }

    const auto amplitude = clampAmplitude(sample.amplitude);
    if (sample.beamIndex == 0) {
        candidate->beam0Peak = std::max(candidate->beam0Peak, amplitude);
        candidate->hasBeam0 = true;
    } else if (sample.beamIndex == 1) {
        candidate->beam1Peak = std::max(candidate->beam1Peak, amplitude);
        candidate->hasBeam1 = true;
    }
}

std::shared_ptr<const BearingSnapshot> BearingAggregator::closeOpenWindow(
    BearingAggregatorCounters& deltaCounters,
    BearingAggregatorTiming* timing)
{
    const bool hasCandidates = m_openWindow
        && (usesFlatCandidateStorage() ? m_openWindow->usedCandidateCount > 0
                                       : !m_openWindow->candidateMap.empty());
    if (!hasCandidates) {
        m_openWindow.reset();
        return {};
    }

    const auto snapshotBuildStartedAt = Clock::now();
    Clock::duration estimateCalculationElapsed{};
    auto snapshot = std::make_shared<BearingSnapshot>();
    snapshot->sequenceId = m_nextSnapshotSequenceId++;
    snapshot->createdUtcNs = currentBearingSnapshotUtcNs();

    const auto handleCandidate = [&](const BearingCandidateAggregate& candidate) {
        if (!candidate.hasBeam0 || !candidate.hasBeam1) {
            ++m_counters.incompleteCandidates;
            ++deltaCounters.incompleteCandidates;
            ++snapshot->counters.incompleteCandidates;
            if (!candidate.hasBeam0) {
                ++m_counters.missingBeam0Candidates;
                ++deltaCounters.missingBeam0Candidates;
                ++snapshot->counters.missingBeam0Candidates;
            }
            if (!candidate.hasBeam1) {
                ++m_counters.missingBeam1Candidates;
                ++deltaCounters.missingBeam1Candidates;
                ++snapshot->counters.missingBeam1Candidates;
            }
            return;
        }

        ++m_counters.completeCandidates;
        ++deltaCounters.completeCandidates;
        ++snapshot->counters.completeCandidates;
        const auto estimateStartedAt = Clock::now();
        if (auto estimate = makeEstimate(candidate)) {
            snapshot->estimates.push_back(*estimate);
        }
        estimateCalculationElapsed += Clock::now() - estimateStartedAt;
    };

    if (usesFlatCandidateStorage()) {
        const auto count = std::min(m_openWindow->candidateVector.size(),
                                    m_openWindow->candidateUsed.size());
        for (std::size_t index = 0; index < count; ++index) {
            if (m_openWindow->candidateUsed[index] == 0) {
                continue;
            }
            handleCandidate(m_openWindow->candidateVector[index]);
        }
    } else {
        for (const auto& [key, candidate] : m_openWindow->candidateMap) {
            (void)key;
            handleCandidate(candidate);
        }
    }

    snapshot->counters.producedEstimates =
        static_cast<std::uint64_t>(snapshot->estimates.size());
    deltaCounters.producedEstimates += snapshot->counters.producedEstimates;
    m_counters.producedEstimates += snapshot->counters.producedEstimates;
    ++deltaCounters.producedSnapshots;
    ++m_counters.producedSnapshots;

    if (timing) {
        const auto snapshotBuildElapsed = Clock::now() - snapshotBuildStartedAt;
        timing->estimateCalculation += estimateCalculationElapsed;
        timing->snapshotBuild += snapshotBuildElapsed >= estimateCalculationElapsed
            ? snapshotBuildElapsed - estimateCalculationElapsed
            : Clock::duration{};
    }

    m_openWindow.reset();
    return snapshot;
}

std::optional<BearingEstimate> BearingAggregator::makeEstimate(
    const BearingCandidateAggregate& candidate) const
{
    const double quality = candidateQuality(candidate.beam0Peak, candidate.beam1Peak);
    if (quality < m_config.minQuality) {
        return std::nullopt;
    }

    const double difference = beamDifference(candidate.beam0Peak, candidate.beam1Peak);
    const double bearingAzimuthDeg = normalizeAzimuthDeg(
        candidate.antennaAzimuthDeg - difference * m_config.beamHalfSeparationDeg);
    const std::uint64_t representativeSampleIndex =
        candidate.firstSampleIndex
        + (candidate.lastSampleIndex - candidate.firstSampleIndex) / 2;

    return BearingEstimate{
        candidate.bandIndex,
        candidate.centerFrequencyHz,
        candidate.frequencyBin,
        representativeSampleIndex,
        utcNsForSample(representativeSampleIndex),
        candidate.antennaAzimuthDeg,
        bearingAzimuthDeg,
        quality,
        candidate.beam0Peak,
        candidate.beam1Peak,
    };
}

} // namespace siriusscope::pipeline
