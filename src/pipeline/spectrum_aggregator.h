#pragma once

#include "core/domain_models.h"
#include "pipeline/signal_block.h"
#include "pipeline/spectrum_snapshot.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

enum class SpectrumWindowIndexMode
{
    ExactInt128,
    DivisibleSamplePeriod,
    IncrementalMonotonic,
};

struct SpectrumAggregatorConfig
{
    int renderBinCount = 1024;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    std::uint64_t snapshotPeriodNs = 20'000'000;
    int amplitudeFloor = 0;
    bool separateBeams = true;
    core::TimeBase timeBase{};
    bool trustedSamples = true;
    SpectrumWindowIndexMode windowIndexMode =
        SpectrumWindowIndexMode::IncrementalMonotonic;
    bool useFastWindowIndex = true;
    bool useFastBinIndex = true;
    bool useFixedBandSummaryStorage = true;
    bool enableDetailedTiming = false;
    std::size_t bandCapacity = 0;
};

struct SpectrumAggregatorCounters
{
    std::uint64_t consumedBlocks = 0;
    std::uint64_t consumedSamples = 0;
    std::uint64_t producedSnapshots = 0;
    std::uint64_t invalidSamples = 0;
    std::uint64_t outOfRangeSamples = 0;
};

struct SpectrumAggregatorTiming
{
    std::chrono::steady_clock::duration total{};
    std::chrono::steady_clock::duration sampleLoop{};
    std::chrono::steady_clock::duration windowCalculation{};
    std::chrono::steady_clock::duration binCalculation{};
    std::chrono::steady_clock::duration binUpdate{};
    std::chrono::steady_clock::duration bandSummaryUpdate{};
    std::chrono::steady_clock::duration closeWindow{};
    std::chrono::steady_clock::duration snapshotBuild{};
};

struct SpectrumAggregationResult
{
    std::vector<std::shared_ptr<const SpectrumSnapshot>> snapshots;
    SpectrumAggregatorCounters deltaCounters;
    SpectrumAggregatorTiming timing;
    bool usedFastWindowIndex = false;
    bool usedFastBinIndex = false;
    bool usedFastBandSummaryStorage = false;
    bool usedIncrementalWindowIndex = false;
    bool usedBlockLocalAccumulation = false;
    std::uint64_t incrementalWindowFallbacks = 0;
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
        std::vector<SpectrumBandSummary> bandSummaryVector;
        std::vector<std::uint8_t> bandSummaryUsed;
        std::size_t usedBandSummaryCount = 0;
        bool hasSamples = false;
    };

    struct LocalBinAccumulator
    {
        std::uint16_t totalPeak = 0;
        std::uint16_t beam0Peak = 0;
        std::uint16_t beam1Peak = 0;
        std::uint16_t hitCount = 0;
        std::uint8_t used = 0;
    };

    struct LocalBandAccumulator
    {
        int bandIndex = 0;
        std::uint64_t sampleCount = 0;
        std::uint16_t totalPeak = 0;
        std::uint16_t beam0Peak = 0;
        std::uint16_t beam1Peak = 0;
        std::uint8_t used = 0;
    };

    void prepareDerivedConfig();
    bool usesFixedBandSummaryStorage() const noexcept;
    bool usesBlockLocalFastPath() const noexcept;
    bool hasValidUntrustedSample(const core::SignalSample& sample) const;
    bool hasFixedBandSummarySlot(int bandIndex) const noexcept;
    std::optional<std::uint64_t> exactWindowForSample(
        std::uint64_t sampleIndex) const;
    std::optional<std::uint64_t> firstSampleIndexForWindow(
        std::uint64_t windowIndex) const;
    std::optional<std::uint64_t> windowForSample(std::uint64_t sampleIndex);
    std::optional<std::uint64_t> incrementalWindowForSample(
        std::uint64_t sampleIndex);
    void resetIncrementalWindowState() noexcept;
    void primeIncrementalWindowState(
        std::uint64_t sampleIndex,
        std::optional<std::uint64_t> windowIndex);
    int binForFrequency(std::int64_t frequencyHz) const noexcept;
    static std::uint16_t clampAmplitude(int amplitude) noexcept;
    static void incrementHitCount(SpectrumBin& bin) noexcept;
    static std::uint16_t saturatedAddHitCount(std::uint16_t left,
                                              std::uint16_t right) noexcept;

    void openWindow(std::uint64_t windowIndex, std::uint64_t sampleIndex);
    std::shared_ptr<const SpectrumSnapshot> closeOpenWindow(
        SpectrumAggregatorTiming* timing = nullptr);
    void updateOpenWindowBounds(const core::SignalSample& sample);
    void updateBinForSample(const core::SignalSample& sample, int binIndex);
    void updateBandSummaryForSample(const core::SignalSample& sample,
                                    SpectrumAggregatorCounters& deltaCounters);
    SpectrumBandSummary* bandSummaryForSample(int bandIndex);
    void prepareLocalAccumulationBuffers();
    void resetLocalAccumulationTouched();
    void updateLocalWindowBounds(const core::SignalSample& sample);
    void updateLocalBinForSample(const core::SignalSample& sample, int binIndex);
    void updateLocalBandSummaryForSample(const core::SignalSample& sample);
    void mergeLocalAccumulationIntoOpenWindow();

    SpectrumAggregatorConfig m_config;
    SpectrumAggregatorCounters m_counters;
    std::optional<OpenWindow> m_openWindow;
    std::uint64_t m_nextSnapshotSequenceId = 1;
    std::uint64_t m_samplesPerWindow = 0;
    bool m_canUseFastWindowIndex = false;
    bool m_canUseIncrementalWindowIndex = false;
    std::optional<std::uint64_t> m_incrementalWindowIndex;
    std::optional<std::uint64_t> m_incrementalNextWindowStartSampleIndex;
    std::optional<std::uint64_t> m_lastWindowSampleIndex;
    std::uint64_t m_incrementalWindowFallbacks = 0;
    bool m_canUseFastBinIndex = false;
    std::int64_t m_fastBinRangeHz = 0;
    std::int64_t m_fastBinMultiplier = 0;
    std::vector<LocalBinAccumulator> m_localBins;
    std::vector<std::uint32_t> m_touchedBins;
    std::vector<LocalBandAccumulator> m_localBands;
    std::vector<std::uint32_t> m_touchedBands;
    bool m_localHasSamples = false;
    std::uint64_t m_localFirstSampleIndex = 0;
    std::uint64_t m_localLastSampleIndex = 0;
};

} // namespace siriusscope::pipeline
