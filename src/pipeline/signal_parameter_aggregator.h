#pragma once

#include "pipeline/signal_block.h"
#include "pipeline/signal_parameter_snapshot.h"
#include "processing/signal_parameter_accumulator.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>

namespace siriusscope::pipeline {

processing::SignalParameterEstimatorConfig defaultSignalParameterAggregatorEstimatorConfig();

struct SignalParameterAggregatorConfig
{
    processing::SignalParameterEstimatorConfig estimatorConfig =
        defaultSignalParameterAggregatorEstimatorConfig();
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

private:
    struct BandSampleSpan
    {
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t lastSampleIndex = 0;
        bool hasSamples = false;
    };

    void updateBandSpans(std::span<const core::SignalSample> samples);

    SignalParameterAggregatorConfig m_config;
    processing::SignalParameterAccumulator m_accumulator;
    std::map<int, BandSampleSpan> m_bandSpans;
    mutable std::uint64_t m_nextSequenceId = 1;
};

} // namespace siriusscope::pipeline
