#include "pipeline/signal_parameter_aggregator.h"

#include <algorithm>
#include <utility>

namespace siriusscope::pipeline {
namespace {

bool hasValidSample(const core::SignalSample& sample)
{
    return core::validateAmplitude(sample.amplitude).isValid()
        && core::validateBandIndex(sample.bandIndex, core::defaultRuntimeCapabilities()).isValid()
        && core::validateBeamIndex(sample.beamIndex, core::defaultRuntimeCapabilities()).isValid()
        && core::validateSystemFrequency(sample.absoluteFrequencyHz).isValid();
}

std::uint64_t saturatedDelta(std::size_t after, std::size_t before)
{
    return after >= before ? static_cast<std::uint64_t>(after - before) : 0;
}

} // namespace

processing::SignalParameterEstimatorConfig defaultSignalParameterAggregatorEstimatorConfig()
{
    processing::SignalParameterEstimatorConfig config;
    config.ingestMode = processing::SignalParameterIngestMode::Streaming;
    return config;
}

SignalParameterAggregator::SignalParameterAggregator(SignalParameterAggregatorConfig config)
    : m_config(std::move(config))
    , m_accumulator(m_config.estimatorConfig)
{
}

void SignalParameterAggregator::reset()
{
    m_accumulator.reset();
    m_bandSpans.clear();
    m_nextSequenceId = 1;
}

void SignalParameterAggregator::setConfig(SignalParameterAggregatorConfig config)
{
    m_config = std::move(config);
    m_accumulator = processing::SignalParameterAccumulator(m_config.estimatorConfig);
    m_bandSpans.clear();
    m_nextSequenceId = 1;
}

SignalParameterAggregationResult SignalParameterAggregator::consume(const SignalBlock& block)
{
    return consume(block.samples());
}

SignalParameterAggregationResult SignalParameterAggregator::consume(
    std::span<const core::SignalSample> samples)
{
    SignalParameterAggregationResult result;
    if (samples.empty()) {
        return result;
    }

    const auto acceptedBefore = m_accumulator.acceptedSampleCount();
    const auto rejectedBefore = m_accumulator.rejectedSampleCount();
    const auto pulseCountBefore = m_accumulator.pulseCount();

    updateBandSpans(samples);
    m_accumulator.ingest(samples);

    result.acceptedSampleDelta =
        saturatedDelta(m_accumulator.acceptedSampleCount(), acceptedBefore);
    result.rejectedSampleDelta =
        saturatedDelta(m_accumulator.rejectedSampleCount(), rejectedBefore);
    result.pulseCountDelta = saturatedDelta(m_accumulator.pulseCount(), pulseCountBefore);
    result.snapshot = makeSnapshot();
    return result;
}

std::shared_ptr<const SignalParameterSnapshot> SignalParameterAggregator::makeSnapshot() const
{
    auto snapshot = std::make_shared<SignalParameterSnapshot>();
    snapshot->sequenceId = m_nextSequenceId++;
    snapshot->createdUtcNs = currentSignalParameterSnapshotUtcNs();
    snapshot->acceptedSampleCount =
        static_cast<std::uint64_t>(m_accumulator.acceptedSampleCount());
    snapshot->rejectedSampleCount =
        static_cast<std::uint64_t>(m_accumulator.rejectedSampleCount());
    snapshot->pulseCount = static_cast<std::uint64_t>(m_accumulator.pulseCount());

    const auto parameters = m_accumulator.finalize();
    snapshot->bands.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        BandSignalParametersSummary summary;
        summary.bandIndex = parameter.bandIndex;
        summary.frequenciesHz = parameter.frequenciesHz;
        summary.pulseRepetitionPeriodUs = parameter.pulseRepetitionPeriodUs;
        summary.pulseWidthUs = parameter.pulseWidthUs;
        summary.pulseCount = static_cast<std::uint64_t>(parameter.pulseCount);

        const auto span = m_bandSpans.find(parameter.bandIndex);
        if (span != m_bandSpans.end() && span->second.hasSamples) {
            summary.firstSampleIndex = span->second.firstSampleIndex;
            summary.lastSampleIndex = span->second.lastSampleIndex;
        }

        snapshot->bands.push_back(std::move(summary));
    }

    return snapshot;
}

void SignalParameterAggregator::updateBandSpans(std::span<const core::SignalSample> samples)
{
    for (const auto& sample : samples) {
        if (!hasValidSample(sample)) {
            continue;
        }

        auto& span = m_bandSpans[sample.bandIndex];
        if (!span.hasSamples) {
            span.firstSampleIndex = sample.sampleIndex;
            span.lastSampleIndex = sample.sampleIndex;
            span.hasSamples = true;
            continue;
        }

        span.firstSampleIndex = std::min(span.firstSampleIndex, sample.sampleIndex);
        span.lastSampleIndex = std::max(span.lastSampleIndex, sample.sampleIndex);
    }
}

} // namespace siriusscope::pipeline
