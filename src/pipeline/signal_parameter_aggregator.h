#pragma once

#include "pipeline/signal_block.h"
#include "pipeline/signal_parameter_snapshot.h"
#include "processing/signal_parameter_accumulator.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

processing::SignalParameterEstimatorConfig defaultSignalParameterAggregatorEstimatorConfig();

struct SignalParameterAggregatorConfig
{
    processing::SignalParameterEstimatorConfig estimatorConfig =
        defaultSignalParameterAggregatorEstimatorConfig();
    std::chrono::milliseconds snapshotPeriod{50};
    bool publishSnapshotEveryBlock = false;
};

struct SignalParameterAggregationResult
{
    std::shared_ptr<const SignalParameterSnapshot> snapshot;
    std::uint64_t acceptedSampleDelta = 0;
    std::uint64_t rejectedSampleDelta = 0;
    std::uint64_t pulseCountDelta = 0;
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
    bool usesStreamingSinglePass() const noexcept;
    bool shouldPublishSnapshot(std::chrono::steady_clock::time_point now) const;
    void markSnapshotPublished(std::chrono::steady_clock::time_point now);
    void prepareBandSpanVectorStorage();
    void resetBandSpanStorage();
    BandSampleSpan* spanForBand(int bandIndex);
    const BandSampleSpan* spanForBandIfUsed(int bandIndex) const;

    SignalParameterAggregatorConfig m_config;
    processing::SignalParameterAccumulator m_accumulator;
    std::vector<BandSampleSpan> m_bandSpanVector;
    std::vector<bool> m_bandSpanVectorUsed;
    std::map<int, BandSampleSpan> m_bandSpans;
    mutable std::uint64_t m_nextSequenceId = 1;
    std::chrono::steady_clock::time_point m_lastSnapshotAt{};
    bool m_snapshotDirty = false;
    bool m_forceNextSnapshot = true;
};

} // namespace siriusscope::pipeline
