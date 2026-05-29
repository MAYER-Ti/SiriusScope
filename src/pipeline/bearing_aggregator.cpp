#include "pipeline/bearing_aggregator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace siriusscope::pipeline {
namespace {

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
    : m_config(std::move(config))
{
}

void BearingAggregator::reset()
{
    m_counters = {};
    m_openWindow.reset();
    m_nextSnapshotSequenceId = 1;
}

void BearingAggregator::setConfig(BearingAggregatorConfig config)
{
    m_config = std::move(config);
    reset();
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
    result.snapshots.reserve(2);

    const double blockAntennaAzimuthDeg =
        antennaAzimuthDeg.value_or(m_config.fallbackAntennaAzimuthDeg);

    for (const auto& sample : samples) {
        ++m_counters.consumedSamples;
        ++result.deltaCounters.consumedSamples;

        const auto windowIndex = windowForSample(sample.sampleIndex);
        if (!windowIndex || sample.amplitude <= 0) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
            continue;
        }

        if (sample.amplitude < m_config.amplitudeFloor) {
            continue;
        }

        const int binIndex = binForFrequency(sample.absoluteFrequencyHz);
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
            if (auto snapshot = closeOpenWindow(result.deltaCounters)) {
                result.snapshots.push_back(std::move(snapshot));
            }
            openWindow(*windowIndex, sample.sampleIndex, blockAntennaAzimuthDeg);
        }

        addSampleToOpenWindow(sample, binIndex);
    }

    return result;
}

BearingAggregationResult BearingAggregator::flush()
{
    BearingAggregationResult result;
    if (auto snapshot = closeOpenWindow(result.deltaCounters)) {
        result.snapshots.push_back(std::move(snapshot));
    }
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
    m_openWindow = std::move(window);
}

void BearingAggregator::addSampleToOpenWindow(const core::SignalSample& sample,
                                              int binIndex)
{
    if (!m_openWindow || binIndex < 0) {
        return;
    }

    m_openWindow->firstSampleIndex =
        std::min(m_openWindow->firstSampleIndex, sample.sampleIndex);
    m_openWindow->lastSampleIndex =
        std::max(m_openWindow->lastSampleIndex, sample.sampleIndex);

    CandidateKey key{sample.bandIndex, static_cast<std::uint32_t>(binIndex)};
    auto& candidate = m_openWindow->candidates[key];
    candidate.bandIndex = sample.bandIndex;
    candidate.frequencyBin = static_cast<std::uint32_t>(binIndex);
    candidate.centerFrequencyHz = centerFrequencyForBin(candidate.frequencyBin);
    candidate.antennaAzimuthDeg = m_openWindow->antennaAzimuthDeg;
    if (!candidate.hasSamples) {
        candidate.firstSampleIndex = sample.sampleIndex;
        candidate.lastSampleIndex = sample.sampleIndex;
        candidate.hasSamples = true;
    } else {
        candidate.firstSampleIndex =
            std::min(candidate.firstSampleIndex, sample.sampleIndex);
        candidate.lastSampleIndex =
            std::max(candidate.lastSampleIndex, sample.sampleIndex);
    }

    const auto amplitude = clampAmplitude(sample.amplitude);
    if (sample.beamIndex == 0) {
        candidate.beam0Peak = std::max(candidate.beam0Peak, amplitude);
        candidate.hasBeam0 = true;
    } else if (sample.beamIndex == 1) {
        candidate.beam1Peak = std::max(candidate.beam1Peak, amplitude);
        candidate.hasBeam1 = true;
    }
}

std::shared_ptr<const BearingSnapshot> BearingAggregator::closeOpenWindow(
    BearingAggregatorCounters& deltaCounters)
{
    if (!m_openWindow || m_openWindow->candidates.empty()) {
        m_openWindow.reset();
        return {};
    }

    auto snapshot = std::make_shared<BearingSnapshot>();
    snapshot->sequenceId = m_nextSnapshotSequenceId++;
    snapshot->createdUtcNs = currentBearingSnapshotUtcNs();

    for (const auto& [key, candidate] : m_openWindow->candidates) {
        (void)key;
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
            continue;
        }

        ++m_counters.completeCandidates;
        ++deltaCounters.completeCandidates;
        ++snapshot->counters.completeCandidates;
        if (auto estimate = makeEstimate(candidate)) {
            snapshot->estimates.push_back(*estimate);
        }
    }

    snapshot->counters.producedEstimates =
        static_cast<std::uint64_t>(snapshot->estimates.size());
    deltaCounters.producedEstimates += snapshot->counters.producedEstimates;
    m_counters.producedEstimates += snapshot->counters.producedEstimates;
    ++deltaCounters.producedSnapshots;
    ++m_counters.producedSnapshots;

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
