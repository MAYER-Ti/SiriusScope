#include "pipeline/spectrum_aggregator.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace siriusscope::pipeline {

SpectrumAggregator::SpectrumAggregator(SpectrumAggregatorConfig config)
    : m_config(std::move(config))
{
}

void SpectrumAggregator::reset()
{
    m_counters = {};
    m_openWindow.reset();
    m_nextSnapshotSequenceId = 1;
}

void SpectrumAggregator::setConfig(SpectrumAggregatorConfig config)
{
    m_config = std::move(config);
    reset();
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
    SpectrumAggregationResult result;
    result.snapshots.reserve(2);

    for (const auto& sample : samples) {
        ++m_counters.consumedSamples;
        ++result.deltaCounters.consumedSamples;

        const auto windowIndex = windowForSample(sample.sampleIndex);
        if (!windowIndex || sample.amplitude <= 0) {
            ++m_counters.invalidSamples;
            ++result.deltaCounters.invalidSamples;
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

        if (sample.amplitude < m_config.amplitudeFloor) {
            continue;
        }

        if (!m_openWindow) {
            openWindow(*windowIndex, sample.sampleIndex);
        } else if (m_openWindow->windowIndex != *windowIndex) {
            if (auto snapshot = closeOpenWindow()) {
                result.snapshots.push_back(std::move(snapshot));
                ++result.deltaCounters.producedSnapshots;
            }
            openWindow(*windowIndex, sample.sampleIndex);
        }

        addSampleToOpenWindow(sample, binIndex);
    }

    return result;
}

SpectrumAggregationResult SpectrumAggregator::flush()
{
    SpectrumAggregationResult result;
    if (auto snapshot = closeOpenWindow()) {
        result.snapshots.push_back(std::move(snapshot));
        ++result.deltaCounters.producedSnapshots;
    }
    return result;
}

std::optional<std::uint64_t> SpectrumAggregator::windowForSample(
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

int SpectrumAggregator::binForFrequency(std::int64_t frequencyHz) const noexcept
{
    if (m_config.renderBinCount <= 0 || m_config.sourceMaxHz <= m_config.sourceMinHz
        || frequencyHz < m_config.sourceMinHz || frequencyHz > m_config.sourceMaxHz) {
        return -1;
    }
    if (m_config.renderBinCount == 1) {
        return 0;
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

void SpectrumAggregator::openWindow(std::uint64_t windowIndex,
                                    std::uint64_t sampleIndex)
{
    OpenWindow window;
    window.windowIndex = windowIndex;
    window.firstSampleIndex = sampleIndex;
    window.lastSampleIndex = sampleIndex;
    window.bins.assign(static_cast<std::size_t>(std::max(0, m_config.renderBinCount)),
                       SpectrumBin{});
    m_openWindow = std::move(window);
}

std::shared_ptr<const SpectrumSnapshot> SpectrumAggregator::closeOpenWindow()
{
    if (!m_openWindow || !m_openWindow->hasSamples) {
        m_openWindow.reset();
        return {};
    }

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
    snapshot->bandSummaries = std::move(m_openWindow->bandSummaries);
    snapshot->counters.producedSnapshots = m_counters.producedSnapshots + 1;
    snapshot->counters.invalidSamples = m_counters.invalidSamples;
    snapshot->counters.outOfRangeSamples = m_counters.outOfRangeSamples;

    ++m_counters.producedSnapshots;
    m_openWindow.reset();
    return snapshot;
}

void SpectrumAggregator::addSampleToOpenWindow(const core::SignalSample& sample,
                                               int binIndex)
{
    if (!m_openWindow || binIndex < 0
        || static_cast<std::size_t>(binIndex) >= m_openWindow->bins.size()) {
        return;
    }

    m_openWindow->firstSampleIndex =
        std::min(m_openWindow->firstSampleIndex, sample.sampleIndex);
    m_openWindow->lastSampleIndex =
        std::max(m_openWindow->lastSampleIndex, sample.sampleIndex);
    m_openWindow->hasSamples = true;

    const auto amplitude = clampAmplitude(sample.amplitude);
    auto& bin = m_openWindow->bins[static_cast<std::size_t>(binIndex)];
    bin.totalPeak = std::max(bin.totalPeak, amplitude);
    if (m_config.separateBeams && sample.beamIndex == 0) {
        bin.beam0Peak = std::max(bin.beam0Peak, amplitude);
    } else if (m_config.separateBeams && sample.beamIndex == 1) {
        bin.beam1Peak = std::max(bin.beam1Peak, amplitude);
    }
    incrementHitCount(bin);

    auto& summary = bandSummaryFor(sample.bandIndex);
    ++summary.sampleCount;
    summary.totalPeak = std::max(summary.totalPeak, amplitude);
    if (m_config.separateBeams && sample.beamIndex == 0) {
        summary.beam0Peak = std::max(summary.beam0Peak, amplitude);
    } else if (m_config.separateBeams && sample.beamIndex == 1) {
        summary.beam1Peak = std::max(summary.beam1Peak, amplitude);
    }
}

SpectrumBandSummary& SpectrumAggregator::bandSummaryFor(int bandIndex)
{
    auto found = std::find_if(m_openWindow->bandSummaries.begin(),
                              m_openWindow->bandSummaries.end(),
                              [bandIndex](const auto& summary) {
                                  return summary.bandIndex == bandIndex;
                              });
    if (found != m_openWindow->bandSummaries.end()) {
        return *found;
    }

    m_openWindow->bandSummaries.push_back(SpectrumBandSummary{});
    auto& summary = m_openWindow->bandSummaries.back();
    summary.bandIndex = bandIndex;
    return summary;
}

} // namespace siriusscope::pipeline
