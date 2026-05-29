#pragma once

#include "core/domain_models.h"
#include "pipeline/signal_block.h"
#include "pipeline/spectrum_snapshot.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

struct SpectrumAggregatorConfig
{
    int renderBinCount = 1024;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    std::uint64_t snapshotPeriodNs = 20'000'000;
    int amplitudeFloor = 0;
    bool separateBeams = true;
    core::TimeBase timeBase{};
};

struct SpectrumAggregatorCounters
{
    std::uint64_t consumedBlocks = 0;
    std::uint64_t consumedSamples = 0;
    std::uint64_t producedSnapshots = 0;
    std::uint64_t invalidSamples = 0;
    std::uint64_t outOfRangeSamples = 0;
};

struct SpectrumAggregationResult
{
    std::vector<std::shared_ptr<const SpectrumSnapshot>> snapshots;
    SpectrumAggregatorCounters deltaCounters;
};

class SpectrumAggregator
{
public:
    explicit SpectrumAggregator(SpectrumAggregatorConfig config = {});

    void reset();
    void setConfig(SpectrumAggregatorConfig config);
    const SpectrumAggregatorConfig& config() const noexcept { return m_config; }

    SpectrumAggregationResult consume(const SignalBlock& block);
    SpectrumAggregationResult consume(std::span<const core::SignalSample> samples);
    SpectrumAggregationResult flush();

    SpectrumAggregatorCounters counters() const noexcept { return m_counters; }

private:
    struct OpenWindow
    {
        std::uint64_t windowIndex = 0;
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t lastSampleIndex = 0;
        std::vector<SpectrumBin> bins;
        std::vector<SpectrumBandSummary> bandSummaries;
        bool hasSamples = false;
    };

    std::optional<std::uint64_t> windowForSample(std::uint64_t sampleIndex) const;
    int binForFrequency(std::int64_t frequencyHz) const noexcept;
    static std::uint16_t clampAmplitude(int amplitude) noexcept;
    static void incrementHitCount(SpectrumBin& bin) noexcept;

    void openWindow(std::uint64_t windowIndex, std::uint64_t sampleIndex);
    std::shared_ptr<const SpectrumSnapshot> closeOpenWindow();
    void addSampleToOpenWindow(const core::SignalSample& sample, int binIndex);
    SpectrumBandSummary& bandSummaryFor(int bandIndex);

    SpectrumAggregatorConfig m_config;
    SpectrumAggregatorCounters m_counters;
    std::optional<OpenWindow> m_openWindow;
    std::uint64_t m_nextSnapshotSequenceId = 1;
};

} // namespace siriusscope::pipeline
