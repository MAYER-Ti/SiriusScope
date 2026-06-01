#pragma once

#include "pipeline/signal_block.h"
#include "pipeline/signal_parameter_snapshot.h"
#include "processing/signal_parameter_accumulator.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

processing::SignalParameterEstimatorConfig defaultSignalParameterAggregatorEstimatorConfig();

enum class SignalParameterSnapshotPolicy
{
    WallClockPeriod,
    ProcessedBlockInterval,
    SourceTimePeriod,
    ManualOnly,
};

struct SignalParameterAggregatorConfig
{
    processing::SignalParameterEstimatorConfig estimatorConfig =
        defaultSignalParameterAggregatorEstimatorConfig();
    std::chrono::milliseconds snapshotPeriod{50};
    bool publishSnapshotEveryBlock = false;
    SignalParameterSnapshotPolicy snapshotPolicy =
        SignalParameterSnapshotPolicy::ProcessedBlockInterval;
    std::uint64_t snapshotBlockInterval = 20;
    std::chrono::milliseconds sourceTimeSnapshotPeriod{500};
};

struct SignalParameterAggregatorTiming
{
    std::chrono::steady_clock::duration ingest{};
    std::chrono::steady_clock::duration snapshotDecision{};
    std::chrono::steady_clock::duration finalize{};
    std::chrono::steady_clock::duration snapshotBuild{};
    std::chrono::steady_clock::duration total{};
};

struct SignalParameterAggregationResult
{
    std::shared_ptr<const SignalParameterSnapshot> snapshot;
    SignalParameterAggregatorTiming timing;
    std::uint64_t acceptedSampleDelta = 0;
    std::uint64_t rejectedSampleDelta = 0;
    std::uint64_t pulseCountDelta = 0;
    bool usedTrustedFixedBandFastPath = false;
    bool snapshotPublished = false;
};

class SignalParameterAggregator
{
public:
    explicit SignalParameterAggregator(SignalParameterAggregatorConfig config = {});

    void reset();
    void setConfig(SignalParameterAggregatorConfig config);

    SignalParameterAggregationResult consume(const SignalBlock& block);
    SignalParameterAggregationResult consume(std::span<const core::SignalSample> samples);

    std::shared_ptr<const SignalParameterSnapshot> makeSnapshot() const;
    std::shared_ptr<const SignalParameterSnapshot> forceSnapshot();

private:
    struct BandSampleSpan
    {
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t lastSampleIndex = 0;
        bool hasSamples = false;
    };

    void updateBandSpans(std::span<const core::SignalSample> samples);
    void updateBandSpanForSample(const core::SignalSample& sample);
    void resetFastSpanBuffers();
    void mergeFastSpanUpdates();
    void updateLatestAcceptedSampleIndex(std::uint64_t sampleIndex);
    bool usesStreamingSinglePass() const noexcept;
    bool usesTrustedFixedBandFastPath() const noexcept;
    bool shouldPublishSnapshot(std::chrono::steady_clock::time_point now) const;
    void markSnapshotPublished(std::chrono::steady_clock::time_point now);
    std::vector<processing::SignalParameters> finalizeSignalParameters() const;
    std::shared_ptr<const SignalParameterSnapshot> buildSnapshotFromParameters(
        std::vector<processing::SignalParameters> parameters) const;
    void prepareBandSpanVectorStorage();
    void resetBandSpanStorage();
    BandSampleSpan* spanForBand(int bandIndex);
    const BandSampleSpan* spanForBandIfUsed(int bandIndex) const;

    SignalParameterAggregatorConfig m_config;
    processing::SignalParameterAccumulator m_accumulator;
    std::vector<BandSampleSpan> m_bandSpanVector;
    std::vector<bool> m_bandSpanVectorUsed;
    std::map<int, BandSampleSpan> m_bandSpans;
    std::vector<std::uint64_t> m_fastFirstSampleIndexByBand;
    std::vector<std::uint64_t> m_fastLastSampleIndexByBand;
    std::vector<std::uint8_t> m_fastBandUsedFlags;
    mutable std::uint64_t m_nextSequenceId = 1;
    std::chrono::steady_clock::time_point m_lastSnapshotAt{};
    bool m_snapshotDirty = false;
    bool m_forceNextSnapshot = true;
    std::uint64_t m_processedBlocksSinceSnapshot = 0;
    std::optional<std::uint64_t> m_lastSnapshotSourceLastSampleIndex;
    std::optional<std::uint64_t> m_latestAcceptedSampleIndex;
};

} // namespace siriusscope::pipeline
