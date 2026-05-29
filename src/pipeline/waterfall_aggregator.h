#pragma once

#include "core/domain_models.h"
#include "pipeline/signal_block.h"
#include "pipeline/waterfall_snapshot.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

struct WaterfallAggregatorConfig
{
    int renderBinCount = 1024;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    std::uint64_t rowPeriodNs = 20'000'000;
    int amplitudeFloor = 0;
    core::TimeBase timeBase{};
};

struct WaterfallAggregatorCounters
{
    std::uint64_t consumedBlocks = 0;
    std::uint64_t consumedSamples = 0;
    std::uint64_t producedRows = 0;
    std::uint64_t producedSnapshots = 0;
    std::uint64_t invalidFrequencySamples = 0;
    std::uint64_t outOfRangeSamples = 0;
    std::uint64_t emptyBlocks = 0;
};

struct WaterfallAggregationResult
{
    std::vector<WaterfallSnapshotRow> rows;
    WaterfallAggregatorCounters deltaCounters;
};

class WaterfallAggregator
{
public:
    explicit WaterfallAggregator(WaterfallAggregatorConfig config = {});

    void reset();
    void setConfig(WaterfallAggregatorConfig config);
    const WaterfallAggregatorConfig& config() const noexcept { return m_config; }

    WaterfallAggregationResult consume(const SignalBlock& block);
    WaterfallAggregationResult consume(std::span<const core::SignalSample> samples);
    WaterfallAggregationResult flush();

    std::shared_ptr<const WaterfallSnapshot> makeSnapshot(
        std::vector<WaterfallSnapshotRow> rows);
    WaterfallAggregatorCounters counters() const noexcept { return m_counters; }

private:
    struct OpenRow
    {
        std::uint64_t bucketIndex = 0;
        std::int64_t utcNs = 0;
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t lastSampleIndex = 0;
        std::vector<WaterfallAggregateCell> cells;
        bool hasSamples = false;
    };

    std::optional<std::uint64_t> bucketForSample(std::uint64_t sampleIndex) const;
    std::int64_t utcNsForBucket(std::uint64_t bucketIndex) const;
    int binForFrequency(std::int64_t frequencyHz) const noexcept;
    static std::uint16_t clampAmplitude(int amplitude) noexcept;
    static void incrementHitCount(WaterfallAggregateCell& cell) noexcept;

    void openBucket(std::uint64_t bucketIndex, std::uint64_t sampleIndex);
    std::optional<WaterfallSnapshotRow> closeOpenRow();
    void addSampleToOpenRow(const core::SignalSample& sample);

    WaterfallAggregatorConfig m_config;
    WaterfallAggregatorCounters m_counters;
    std::optional<OpenRow> m_openRow;
    std::uint64_t m_nextSnapshotSequenceId = 1;
};

} // namespace siriusscope::pipeline
