#include "pipeline/signal_parameter_aggregator.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace siriusscope::pipeline {
namespace {

using Clock = std::chrono::steady_clock;

bool hasValidSample(const core::SignalSample& sample)
{
    return core::validateAmplitude(sample.amplitude).isValid()
        && core::validateBandIndex(sample.bandIndex, core::defaultRuntimeCapabilities()).isValid()
        && core::validateBeamIndex(sample.beamIndex, core::defaultRuntimeCapabilities()).isValid()
        && core::validateSystemFrequency(sample.absoluteFrequencyHz).isValid();
}

bool hasTrustedMapBandIndexStorageRange(int bandIndex)
{
    const auto defaultBandCount = core::defaultRuntimeCapabilities().bandCount;
    return bandIndex >= 0 && defaultBandCount > 0 && bandIndex < defaultBandCount;
}

std::uint64_t saturatedDelta(std::size_t after, std::size_t before)
{
    return after >= before ? static_cast<std::uint64_t>(after - before) : 0;
}

void normalizeFixedBandCapacity(processing::SignalParameterEstimatorConfig& config)
{
    config.samplePeriodNs = std::max<std::uint64_t>(1, config.samplePeriodNs);

    if (config.bandStateMode != processing::SignalParameterBandStateMode::FixedBandIndexVector
        || config.bandStateCapacity != 0) {
        return;
    }

    const auto defaultBandCount = core::defaultRuntimeCapabilities().bandCount;
    if (defaultBandCount > 0) {
        config.bandStateCapacity = static_cast<std::size_t>(defaultBandCount);
    }
}

SignalParameterAggregatorConfig normalizeConfig(SignalParameterAggregatorConfig config)
{
    normalizeFixedBandCapacity(config.estimatorConfig);
    return config;
}

std::uint64_t requiredSamplesForSourceTimePeriod(
    std::chrono::milliseconds period,
    std::uint64_t samplePeriodNs)
{
    if (period.count() <= 0) {
        return 0;
    }

    const auto targetNsSigned =
        std::chrono::duration_cast<std::chrono::nanoseconds>(period).count();
    if (targetNsSigned <= 0) {
        return 0;
    }

    const auto targetNs = static_cast<std::uint64_t>(targetNsSigned);
    samplePeriodNs = std::max<std::uint64_t>(1, samplePeriodNs);
    return targetNs / samplePeriodNs + (targetNs % samplePeriodNs == 0 ? 0 : 1);
}

} // namespace

processing::SignalParameterEstimatorConfig defaultSignalParameterAggregatorEstimatorConfig()
{
    processing::SignalParameterEstimatorConfig config;
    config.ingestMode = processing::SignalParameterIngestMode::Streaming;
    config.validationMode = processing::SignalParameterValidationMode::TrustedValidatedSamples;
    config.bandStateMode = processing::SignalParameterBandStateMode::FixedBandIndexVector;
    normalizeFixedBandCapacity(config);
    return config;
}

SignalParameterAggregator::SignalParameterAggregator(SignalParameterAggregatorConfig config)
    : m_config(normalizeConfig(std::move(config)))
    , m_accumulator(m_config.estimatorConfig)
{
    prepareBandSpanVectorStorage();
}

void SignalParameterAggregator::reset()
{
    m_accumulator.reset();
    resetBandSpanStorage();
    m_nextSequenceId = 1;
    m_lastSnapshotAt = {};
    m_snapshotDirty = false;
    m_forceNextSnapshot = true;
    m_processedBlocksSinceSnapshot = 0;
    m_lastSnapshotSourceLastSampleIndex.reset();
    m_latestAcceptedSampleIndex.reset();
}

void SignalParameterAggregator::setConfig(SignalParameterAggregatorConfig config)
{
    m_config = normalizeConfig(std::move(config));
    m_accumulator = processing::SignalParameterAccumulator(m_config.estimatorConfig);
    prepareBandSpanVectorStorage();
    m_nextSequenceId = 1;
    m_lastSnapshotAt = {};
    m_snapshotDirty = false;
    m_forceNextSnapshot = true;
    m_processedBlocksSinceSnapshot = 0;
    m_lastSnapshotSourceLastSampleIndex.reset();
    m_latestAcceptedSampleIndex.reset();
}

SignalParameterAggregationResult SignalParameterAggregator::consume(const SignalBlock& block)
{
    return consume(block.samples());
}

SignalParameterAggregationResult SignalParameterAggregator::consume(
    std::span<const core::SignalSample> samples)
{
    SignalParameterAggregationResult result;
    const auto totalStartedAt = Clock::now();
    if (samples.empty()) {
        result.timing.total = Clock::now() - totalStartedAt;
        return result;
    }

    const auto acceptedBefore = m_accumulator.acceptedSampleCount();
    const auto rejectedBefore = m_accumulator.rejectedSampleCount();
    const auto pulseCountBefore = m_accumulator.pulseCount();

    const auto ingestStartedAt = Clock::now();
    if (usesTrustedFixedBandFastPath()) {
        resetFastSpanBuffers();
        const auto summary = m_accumulator.ingestTrustedFixedBandSamples(
            samples,
            m_fastFirstSampleIndexByBand,
            m_fastLastSampleIndexByBand,
            m_fastBandUsedFlags);
        result.usedTrustedFixedBandFastPath = true;
        mergeFastSpanUpdates();
        if (summary.latestAcceptedSampleIndex) {
            updateLatestAcceptedSampleIndex(*summary.latestAcceptedSampleIndex);
        }
    } else if (usesStreamingSinglePass()) {
        for (const auto& sample : samples) {
            const auto ingestResult = m_accumulator.ingestSample(sample);
            if (ingestResult == processing::SignalParameterSampleIngestResult::Accepted) {
                updateBandSpanForSample(sample);
                updateLatestAcceptedSampleIndex(sample.sampleIndex);
            }
        }
    } else {
        updateBandSpans(samples);
        m_accumulator.ingest(samples);
    }
    result.timing.ingest = Clock::now() - ingestStartedAt;

    result.acceptedSampleDelta =
        saturatedDelta(m_accumulator.acceptedSampleCount(), acceptedBefore);
    result.rejectedSampleDelta =
        saturatedDelta(m_accumulator.rejectedSampleCount(), rejectedBefore);
    result.pulseCountDelta = saturatedDelta(m_accumulator.pulseCount(), pulseCountBefore);

    if (!usesStreamingSinglePass() && result.acceptedSampleDelta > 0) {
        for (const auto& sample : samples) {
            updateLatestAcceptedSampleIndex(sample.sampleIndex);
        }
    }

    if (result.acceptedSampleDelta > 0 || result.rejectedSampleDelta > 0
        || result.pulseCountDelta > 0) {
        m_snapshotDirty = true;
    }

    ++m_processedBlocksSinceSnapshot;

    const auto now = Clock::now();
    const auto decisionStartedAt = Clock::now();
    if (shouldPublishSnapshot(now)) {
        result.timing.snapshotDecision = Clock::now() - decisionStartedAt;

        const auto finalizeStartedAt = Clock::now();
        auto parameters = finalizeSignalParameters();
        result.timing.finalize = Clock::now() - finalizeStartedAt;

        const auto buildStartedAt = Clock::now();
        result.snapshot = buildSnapshotFromParameters(std::move(parameters));
        result.timing.snapshotBuild = Clock::now() - buildStartedAt;
        result.snapshotPublished = result.snapshot != nullptr;
        markSnapshotPublished(now);
        result.timing.total = Clock::now() - totalStartedAt;
        return result;
    }
    result.timing.snapshotDecision = Clock::now() - decisionStartedAt;
    result.timing.total = Clock::now() - totalStartedAt;
    return result;
}

std::shared_ptr<const SignalParameterSnapshot> SignalParameterAggregator::makeSnapshot() const
{
    auto parameters = finalizeSignalParameters();
    return buildSnapshotFromParameters(std::move(parameters));
}

std::vector<processing::SignalParameters>
SignalParameterAggregator::finalizeSignalParameters() const
{
    return m_accumulator.finalize();
}

std::shared_ptr<const SignalParameterSnapshot>
SignalParameterAggregator::buildSnapshotFromParameters(
    std::vector<processing::SignalParameters> parameters) const
{
    auto snapshot = std::make_shared<SignalParameterSnapshot>();
    snapshot->sequenceId = m_nextSequenceId++;
    snapshot->createdUtcNs = currentSignalParameterSnapshotUtcNs();
    snapshot->acceptedSampleCount =
        static_cast<std::uint64_t>(m_accumulator.acceptedSampleCount());
    snapshot->rejectedSampleCount =
        static_cast<std::uint64_t>(m_accumulator.rejectedSampleCount());
    snapshot->pulseCount = static_cast<std::uint64_t>(m_accumulator.pulseCount());

    snapshot->bands.reserve(parameters.size());
    for (const auto& parameter : parameters) {
        BandSignalParametersSummary summary;
        summary.bandIndex = parameter.bandIndex;
        summary.frequenciesHz = parameter.frequenciesHz;
        summary.pulseRepetitionPeriodUs = parameter.pulseRepetitionPeriodUs;
        summary.pulseWidthUs = parameter.pulseWidthUs;
        summary.pulseCount = static_cast<std::uint64_t>(parameter.pulseCount);

        const auto* span = spanForBandIfUsed(parameter.bandIndex);
        if (span && span->hasSamples) {
            summary.firstSampleIndex = span->firstSampleIndex;
            summary.lastSampleIndex = span->lastSampleIndex;
        }

        snapshot->bands.push_back(std::move(summary));
    }

    return snapshot;
}

std::shared_ptr<const SignalParameterSnapshot> SignalParameterAggregator::forceSnapshot()
{
    const auto now = Clock::now();
    auto snapshot = makeSnapshot();
    markSnapshotPublished(now);
    return snapshot;
}

void SignalParameterAggregator::updateBandSpans(std::span<const core::SignalSample> samples)
{
    const bool trustedSamples = m_config.estimatorConfig.validationMode
        == processing::SignalParameterValidationMode::TrustedValidatedSamples;
    const bool trustedMapBandState = trustedSamples
        && m_config.estimatorConfig.bandStateMode
            == processing::SignalParameterBandStateMode::MapByBandIndex;

    for (const auto& sample : samples) {
        if (!trustedSamples && !hasValidSample(sample)) {
            continue;
        }
        if (trustedMapBandState && !hasTrustedMapBandIndexStorageRange(sample.bandIndex)) {
            continue;
        }

        updateBandSpanForSample(sample);
    }
}

void SignalParameterAggregator::updateBandSpanForSample(const core::SignalSample& sample)
{
    auto* span = spanForBand(sample.bandIndex);
    if (!span) {
        return;
    }

    if (!span->hasSamples) {
        span->firstSampleIndex = sample.sampleIndex;
        span->lastSampleIndex = sample.sampleIndex;
        span->hasSamples = true;
        return;
    }

    span->firstSampleIndex = std::min(span->firstSampleIndex, sample.sampleIndex);
    span->lastSampleIndex = std::max(span->lastSampleIndex, sample.sampleIndex);
}

void SignalParameterAggregator::resetFastSpanBuffers()
{
    const auto capacity = m_config.estimatorConfig.bandStateCapacity;
    if (!usesTrustedFixedBandFastPath() || capacity == 0) {
        m_fastFirstSampleIndexByBand.clear();
        m_fastLastSampleIndexByBand.clear();
        m_fastBandUsedFlags.clear();
        return;
    }

    if (m_fastFirstSampleIndexByBand.size() != capacity) {
        m_fastFirstSampleIndexByBand.resize(capacity);
    }
    if (m_fastLastSampleIndexByBand.size() != capacity) {
        m_fastLastSampleIndexByBand.resize(capacity);
    }
    if (m_fastBandUsedFlags.size() != capacity) {
        m_fastBandUsedFlags.resize(capacity);
    }
    std::fill(m_fastBandUsedFlags.begin(), m_fastBandUsedFlags.end(), std::uint8_t{0});
}

void SignalParameterAggregator::mergeFastSpanUpdates()
{
    const auto count = std::min(m_fastBandUsedFlags.size(),
                                std::min(m_fastFirstSampleIndexByBand.size(),
                                         m_fastLastSampleIndexByBand.size()));
    for (std::size_t index = 0; index < count; ++index) {
        if (m_fastBandUsedFlags[index] == 0) {
            continue;
        }

        auto* span = spanForBand(static_cast<int>(index));
        if (!span) {
            continue;
        }

        const auto firstSampleIndex = m_fastFirstSampleIndexByBand[index];
        const auto lastSampleIndex = m_fastLastSampleIndexByBand[index];
        if (!span->hasSamples) {
            span->firstSampleIndex = firstSampleIndex;
            span->lastSampleIndex = lastSampleIndex;
            span->hasSamples = true;
            continue;
        }

        span->firstSampleIndex = std::min(span->firstSampleIndex, firstSampleIndex);
        span->lastSampleIndex = std::max(span->lastSampleIndex, lastSampleIndex);
    }
}

void SignalParameterAggregator::updateLatestAcceptedSampleIndex(std::uint64_t sampleIndex)
{
    if (!m_latestAcceptedSampleIndex || sampleIndex > *m_latestAcceptedSampleIndex) {
        m_latestAcceptedSampleIndex = sampleIndex;
    }
}

bool SignalParameterAggregator::usesStreamingSinglePass() const noexcept
{
    return m_config.estimatorConfig.ingestMode
        == processing::SignalParameterIngestMode::Streaming;
}

bool SignalParameterAggregator::usesTrustedFixedBandFastPath() const noexcept
{
    const auto& estimator = m_config.estimatorConfig;
    return estimator.ingestMode == processing::SignalParameterIngestMode::Streaming
        && estimator.validationMode
            == processing::SignalParameterValidationMode::TrustedValidatedSamples
        && estimator.bandStateMode
            == processing::SignalParameterBandStateMode::FixedBandIndexVector
        && estimator.bandStateCapacity > 0;
}

bool SignalParameterAggregator::shouldPublishSnapshot(Clock::time_point now) const
{
    if (m_config.publishSnapshotEveryBlock) {
        return true;
    }
    if (m_forceNextSnapshot) {
        return true;
    }
    if (!m_snapshotDirty) {
        return false;
    }

    switch (m_config.snapshotPolicy) {
    case SignalParameterSnapshotPolicy::WallClockPeriod:
        if (m_config.snapshotPeriod.count() <= 0) {
            return true;
        }
        if (m_lastSnapshotAt == Clock::time_point{}) {
            return true;
        }
        return now - m_lastSnapshotAt >= m_config.snapshotPeriod;

    case SignalParameterSnapshotPolicy::ProcessedBlockInterval:
        if (m_config.snapshotBlockInterval == 0) {
            return true;
        }
        return m_processedBlocksSinceSnapshot >= m_config.snapshotBlockInterval;

    case SignalParameterSnapshotPolicy::SourceTimePeriod:
        if (m_config.sourceTimeSnapshotPeriod.count() <= 0) {
            return true;
        }
        if (!m_latestAcceptedSampleIndex) {
            return false;
        }
        if (!m_lastSnapshotSourceLastSampleIndex) {
            return true;
        }
        if (*m_latestAcceptedSampleIndex <= *m_lastSnapshotSourceLastSampleIndex) {
            return false;
        }
        return *m_latestAcceptedSampleIndex - *m_lastSnapshotSourceLastSampleIndex
            >= requiredSamplesForSourceTimePeriod(m_config.sourceTimeSnapshotPeriod,
                                                  m_config.estimatorConfig.samplePeriodNs);

    case SignalParameterSnapshotPolicy::ManualOnly:
        return false;
    }

    return false;
}

void SignalParameterAggregator::markSnapshotPublished(Clock::time_point now)
{
    m_lastSnapshotAt = now;
    m_snapshotDirty = false;
    m_forceNextSnapshot = false;
    m_processedBlocksSinceSnapshot = 0;
    if (m_latestAcceptedSampleIndex) {
        m_lastSnapshotSourceLastSampleIndex = m_latestAcceptedSampleIndex;
    }
}

void SignalParameterAggregator::prepareBandSpanVectorStorage()
{
    m_bandSpans.clear();

    if (m_config.estimatorConfig.bandStateMode
        != processing::SignalParameterBandStateMode::FixedBandIndexVector) {
        m_bandSpanVector.clear();
        m_bandSpanVectorUsed.clear();
        m_fastFirstSampleIndexByBand.clear();
        m_fastLastSampleIndexByBand.clear();
        m_fastBandUsedFlags.clear();
        return;
    }

    normalizeFixedBandCapacity(m_config.estimatorConfig);
    m_bandSpanVector.resize(m_config.estimatorConfig.bandStateCapacity);
    m_bandSpanVectorUsed.assign(m_config.estimatorConfig.bandStateCapacity, false);
    m_fastFirstSampleIndexByBand.resize(m_config.estimatorConfig.bandStateCapacity);
    m_fastLastSampleIndexByBand.resize(m_config.estimatorConfig.bandStateCapacity);
    m_fastBandUsedFlags.resize(m_config.estimatorConfig.bandStateCapacity);
}

void SignalParameterAggregator::resetBandSpanStorage()
{
    m_bandSpans.clear();

    if (m_config.estimatorConfig.bandStateMode
        != processing::SignalParameterBandStateMode::FixedBandIndexVector) {
        m_bandSpanVector.clear();
        m_bandSpanVectorUsed.clear();
        return;
    }

    if (m_bandSpanVector.size() != m_config.estimatorConfig.bandStateCapacity
        || m_bandSpanVectorUsed.size() != m_config.estimatorConfig.bandStateCapacity) {
        prepareBandSpanVectorStorage();
    }

    for (auto& span : m_bandSpanVector) {
        span = {};
    }
    std::fill(m_bandSpanVectorUsed.begin(), m_bandSpanVectorUsed.end(), false);
}

SignalParameterAggregator::BandSampleSpan* SignalParameterAggregator::spanForBand(int bandIndex)
{
    if (m_config.estimatorConfig.bandStateMode
        == processing::SignalParameterBandStateMode::FixedBandIndexVector) {
        if (bandIndex < 0) {
            return nullptr;
        }

        const auto vectorIndex = static_cast<std::size_t>(bandIndex);
        if (vectorIndex >= m_config.estimatorConfig.bandStateCapacity
            || vectorIndex >= m_bandSpanVector.size()
            || vectorIndex >= m_bandSpanVectorUsed.size()) {
            return nullptr;
        }

        m_bandSpanVectorUsed[vectorIndex] = true;
        return &m_bandSpanVector[vectorIndex];
    }

    return &m_bandSpans[bandIndex];
}

const SignalParameterAggregator::BandSampleSpan*
SignalParameterAggregator::spanForBandIfUsed(int bandIndex) const
{
    if (m_config.estimatorConfig.bandStateMode
        == processing::SignalParameterBandStateMode::FixedBandIndexVector) {
        if (bandIndex < 0) {
            return nullptr;
        }

        const auto vectorIndex = static_cast<std::size_t>(bandIndex);
        if (vectorIndex >= m_bandSpanVector.size() || vectorIndex >= m_bandSpanVectorUsed.size()
            || !m_bandSpanVectorUsed[vectorIndex]) {
            return nullptr;
        }

        return &m_bandSpanVector[vectorIndex];
    }

    const auto found = m_bandSpans.find(bandIndex);
    return found == m_bandSpans.end() ? nullptr : &found->second;
}

} // namespace siriusscope::pipeline
