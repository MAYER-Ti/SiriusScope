#include "pipeline/waterfall_aggregator.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>

namespace siriusscope::pipeline {
namespace {

std::uint64_t saturatedAdd(std::uint64_t lhs, std::uint64_t rhs)
{
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
}

} // namespace

WaterfallAggregator::WaterfallAggregator(WaterfallAggregatorConfig config)
    : m_config(std::move(config))
{
}

void WaterfallAggregator::reset()
{
    m_counters = {};
    m_openRow.reset();
    m_nextSnapshotSequenceId = 1;
}

void WaterfallAggregator::setConfig(WaterfallAggregatorConfig config)
{
    m_config = std::move(config);
    reset();
}

WaterfallAggregationResult WaterfallAggregator::consume(const SignalBlock& block)
{
    if (block.empty()) {
        ++m_counters.emptyBlocks;
        WaterfallAggregationResult result;
        result.deltaCounters.emptyBlocks = 1;
        return result;
    }

    ++m_counters.consumedBlocks;
    auto result = consume(block.samples());
    result.deltaCounters.consumedBlocks = 1;
    return result;
}

WaterfallAggregationResult WaterfallAggregator::consume(
    std::span<const core::SignalSample> samples)
{
    WaterfallAggregationResult result;
    result.rows.reserve(2);

    for (const auto& sample : samples) {
        ++m_counters.consumedSamples;
        ++result.deltaCounters.consumedSamples;

        const auto bucketIndex = bucketForSample(sample.sampleIndex);
        if (!bucketIndex) {
            ++m_counters.invalidFrequencySamples;
            ++result.deltaCounters.invalidFrequencySamples;
            continue;
        }

        const int bin = binForFrequency(sample.absoluteFrequencyHz);
        if (bin < 0) {
            if (sample.absoluteFrequencyHz <= 0 || m_config.sourceMaxHz <= m_config.sourceMinHz) {
                ++m_counters.invalidFrequencySamples;
                ++result.deltaCounters.invalidFrequencySamples;
            } else {
                ++m_counters.outOfRangeSamples;
                ++result.deltaCounters.outOfRangeSamples;
            }
            continue;
        }

        if (sample.amplitude < m_config.amplitudeFloor) {
            continue;
        }

        if (!m_openRow) {
            openBucket(*bucketIndex, sample.sampleIndex);
        } else if (m_openRow->bucketIndex != *bucketIndex) {
            if (auto closed = closeOpenRow()) {
                result.rows.push_back(std::move(*closed));
                ++m_counters.producedRows;
                ++result.deltaCounters.producedRows;
            }
            openBucket(*bucketIndex, sample.sampleIndex);
        }

        addSampleToOpenRow(sample);
    }

    return result;
}

WaterfallAggregationResult WaterfallAggregator::flush()
{
    WaterfallAggregationResult result;
    if (auto closed = closeOpenRow()) {
        result.rows.push_back(std::move(*closed));
        ++m_counters.producedRows;
        ++result.deltaCounters.producedRows;
    }
    return result;
}

std::shared_ptr<const WaterfallSnapshot> WaterfallAggregator::makeSnapshot(
    std::vector<WaterfallSnapshotRow> rows)
{
    if (rows.empty()) {
        return {};
    }

    auto snapshot = std::make_shared<WaterfallSnapshot>();
    snapshot->sequenceId = m_nextSnapshotSequenceId++;
    snapshot->createdAtUtcNs = currentUtcNs();
    snapshot->sourceMinHz = m_config.sourceMinHz;
    snapshot->sourceMaxHz = m_config.sourceMaxHz;
    snapshot->renderBinCount = m_config.renderBinCount;
    snapshot->rowPeriodNs = m_config.rowPeriodNs;
    snapshot->rows = std::move(rows);
    snapshot->counters.producedRows = m_counters.producedRows;
    snapshot->counters.invalidFrequencySamples = m_counters.invalidFrequencySamples;
    snapshot->counters.outOfRangeSamples = m_counters.outOfRangeSamples;
    snapshot->counters.emptyBlocks = m_counters.emptyBlocks;
    snapshot->counters.producedSnapshots = m_counters.producedSnapshots + 1;

    ++m_counters.producedSnapshots;
    return snapshot;
}

std::optional<std::uint64_t> WaterfallAggregator::bucketForSample(
    std::uint64_t sampleIndex) const
{
    if (m_config.rowPeriodNs == 0 || m_config.timeBase.samplePeriodNs == 0) {
        return std::nullopt;
    }
    if (sampleIndex < m_config.timeBase.firstSampleIndex) {
        return std::nullopt;
    }

    const auto relativeSampleIndex = sampleIndex - m_config.timeBase.firstSampleIndex;
    const auto samplePeriod = static_cast<unsigned __int128>(m_config.timeBase.samplePeriodNs);
    const auto relativeNs =
        static_cast<unsigned __int128>(relativeSampleIndex) * samplePeriod;
    const auto bucket =
        relativeNs / static_cast<unsigned __int128>(m_config.rowPeriodNs);
    if (bucket > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(bucket);
}

std::int64_t WaterfallAggregator::utcNsForBucket(std::uint64_t bucketIndex) const
{
    const auto rowOffset =
        static_cast<unsigned __int128>(bucketIndex)
        * static_cast<unsigned __int128>(m_config.rowPeriodNs);
    const auto maxOffset = static_cast<unsigned __int128>(
        std::numeric_limits<std::int64_t>::max()
        - std::max<std::int64_t>(0, m_config.timeBase.recordingStartUtcNs));
    if (rowOffset > maxOffset) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return m_config.timeBase.recordingStartUtcNs
        + static_cast<std::int64_t>(rowOffset);
}

int WaterfallAggregator::binForFrequency(std::int64_t frequencyHz) const noexcept
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
    const auto denominator = static_cast<__int128>(m_config.sourceMaxHz - m_config.sourceMinHz);
    const auto bin = denominator == 0 ? 0 : numerator / denominator;
    return std::clamp(static_cast<int>(bin), 0, m_config.renderBinCount - 1);
}

std::uint16_t WaterfallAggregator::clampAmplitude(int amplitude) noexcept
{
    return static_cast<std::uint16_t>(
        std::clamp(amplitude, 0, static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
}

void WaterfallAggregator::incrementHitCount(WaterfallAggregateCell& cell) noexcept
{
    if (cell.hitCount < std::numeric_limits<std::uint16_t>::max()) {
        ++cell.hitCount;
    }
}

void WaterfallAggregator::openBucket(std::uint64_t bucketIndex,
                                     std::uint64_t sampleIndex)
{
    OpenRow row;
    row.bucketIndex = bucketIndex;
    row.utcNs = utcNsForBucket(bucketIndex);
    row.firstSampleIndex = sampleIndex;
    row.lastSampleIndex = sampleIndex;
    row.cells.assign(static_cast<std::size_t>(std::max(0, m_config.renderBinCount)),
                     WaterfallAggregateCell{});
    m_openRow = std::move(row);
}

std::optional<WaterfallSnapshotRow> WaterfallAggregator::closeOpenRow()
{
    if (!m_openRow || !m_openRow->hasSamples) {
        m_openRow.reset();
        return std::nullopt;
    }

    WaterfallSnapshotRow row;
    row.utcNs = m_openRow->utcNs;
    row.firstSampleIndex = m_openRow->firstSampleIndex;
    row.lastSampleIndex = m_openRow->lastSampleIndex;
    row.cells = std::move(m_openRow->cells);
    m_openRow.reset();
    return row;
}

void WaterfallAggregator::addSampleToOpenRow(const core::SignalSample& sample)
{
    if (!m_openRow) {
        return;
    }

    const int bin = binForFrequency(sample.absoluteFrequencyHz);
    if (bin < 0 || static_cast<std::size_t>(bin) >= m_openRow->cells.size()) {
        return;
    }

    m_openRow->firstSampleIndex = std::min(m_openRow->firstSampleIndex, sample.sampleIndex);
    m_openRow->lastSampleIndex = std::max(m_openRow->lastSampleIndex, sample.sampleIndex);
    m_openRow->hasSamples = true;

    auto& cell = m_openRow->cells[static_cast<std::size_t>(bin)];
    const auto amplitude = clampAmplitude(sample.amplitude);
    if (sample.beamIndex == 0) {
        cell.beam0Peak = std::max(cell.beam0Peak, amplitude);
        incrementHitCount(cell);
    } else if (sample.beamIndex == 1) {
        cell.beam1Peak = std::max(cell.beam1Peak, amplitude);
        incrementHitCount(cell);
    }
}

} // namespace siriusscope::pipeline
