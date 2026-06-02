#include "pipeline/processing_engine.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace siriusscope::pipeline {
namespace {

using Clock = std::chrono::steady_clock;

} // namespace

ProcessingEngine::ProcessingEngine(BoundedBlockQueue* queue,
                                   PipelineMetrics* metrics,
                                   PipelineDiagnostics* diagnostics,
                                   WaterfallRowQueue* waterfallRows,
                                   WaterfallAggregatorConfig waterfallConfig,
                                   SnapshotExchange<SpectrumSnapshot>* spectrumSnapshots,
                                   SpectrumAggregatorConfig spectrumConfig,
                                   SnapshotExchange<BearingSnapshot>* bearingSnapshots,
                                   BearingAggregatorConfig bearingConfig,
                                   SnapshotExchange<SignalParameterSnapshot>* signalParameterSnapshots,
                                   SignalParameterAggregatorConfig signalParameterConfig,
                                   ProcessingEngineConfig processingConfig)
    : m_queue(queue)
    , m_metrics(metrics)
    , m_diagnostics(diagnostics)
    , m_waterfallRows(waterfallRows)
    , m_spectrumSnapshots(spectrumSnapshots)
    , m_bearingSnapshots(bearingSnapshots)
    , m_signalParameterSnapshots(signalParameterSnapshots)
    , m_processingConfig(processingConfig)
    , m_waterfallAggregator(std::move(waterfallConfig))
    , m_spectrumAggregator(std::move(spectrumConfig))
    , m_bearingAggregator(std::move(bearingConfig))
    , m_signalParameterAggregator(std::move(signalParameterConfig))
{
}

ProcessingEngine::~ProcessingEngine()
{
    stop();
}

core::OperationResult ProcessingEngine::start()
{
    std::lock_guard lock(m_mutex);
    if (m_running) {
        return core::OperationResult::ok();
    }
    if (!m_queue || !m_metrics) {
        return core::OperationResult::failure("processing engine dependencies are not configured");
    }

    m_running = true;
    m_processingBlock = false;
    m_summary = {};
    m_completedQueuePops = 0;
    m_inFlightFanOutBlocks.store(0, std::memory_order_release);
    resetFanOutQueues();
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        m_waterfallAggregator.reset();
        m_nextWaterfallSourceSnapshotSequenceId = 1;
    }
    {
        std::lock_guard spectrumLock(m_spectrumMutex);
        m_spectrumAggregator.reset();
    }
    {
        std::lock_guard bearingLock(m_bearingMutex);
        m_bearingAggregator.reset();
    }
    {
        std::lock_guard signalParameterLock(m_signalParameterMutex);
        m_signalParameterAggregator.reset();
    }
    if (m_processingConfig.processingMode == ProcessingMode::ParallelFanOut) {
        startFanOutWorkers();
    }
    m_worker = std::thread(&ProcessingEngine::workerLoop, this);
    return core::OperationResult::ok();
}

void ProcessingEngine::stop()
{
    if (m_queue) {
        m_queue->shutdown();
    }

    {
        std::lock_guard lock(m_mutex);
        if (!m_running && !m_worker.joinable() && !stageWorkersJoinable()) {
            return;
        }
        m_running = false;
    }

    m_flushCondition.notify_all();
    if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id()) {
        m_worker.join();
    }

    stopFanOutWorkers();

    flushWaterfallRows();
    flushSpectrumSnapshots();
    flushBearingSnapshots();

    {
        std::lock_guard lock(m_mutex);
        m_processingBlock = false;
    }
    m_flushCondition.notify_all();
}

core::OperationResult ProcessingEngine::flush(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        return core::OperationResult::failure("processing flush timeout is invalid");
    }

    bool completed = false;
    const auto targetCompletedQueuePops = m_queue ? m_queue->metrics().pushedBlocks : 0;
    {
        std::unique_lock lock(m_mutex);
        const auto isDrained = [this, targetCompletedQueuePops] {
            return (!m_queue || m_queue->metrics().depth == 0)
                && m_completedQueuePops >= targetCompletedQueuePops
                && !m_processingBlock
                && fanOutDrained();
        };

        completed = isDrained() || m_flushCondition.wait_for(lock, timeout, isDrained);
    }

    if (!completed) {
        return core::OperationResult::failure("processing flush timed out");
    }

    flushWaterfallRows();
    flushSpectrumSnapshots();
    flushBearingSnapshots();
    return core::OperationResult::ok();
}

ProcessingEngineSummary ProcessingEngine::lastSummary() const
{
    std::lock_guard lock(m_mutex);
    return m_summary;
}

ParallelFanOutQueueMetrics ProcessingEngine::parallelFanOutQueueMetrics() const
{
    if (m_processingConfig.processingMode != ProcessingMode::ParallelFanOut) {
        return {};
    }

    std::lock_guard lock(m_stageMutex);
    ParallelFanOutQueueMetrics metrics;
    const auto capacity = m_processingConfig.stageQueueCapacity;
    metrics.waterfallStageQueueDepth =
        m_stageQueues[fanOutStageIndex(FanOutStage::Waterfall)].size();
    metrics.waterfallStageQueueCapacity = capacity;
    metrics.spectrumStageQueueDepth =
        m_stageQueues[fanOutStageIndex(FanOutStage::Spectrum)].size();
    metrics.spectrumStageQueueCapacity = capacity;
    metrics.bearingStageQueueDepth =
        m_stageQueues[fanOutStageIndex(FanOutStage::Bearing)].size();
    metrics.bearingStageQueueCapacity = capacity;
    metrics.signalParameterStageQueueDepth =
        m_stageQueues[fanOutStageIndex(FanOutStage::SignalParameter)].size();
    metrics.signalParameterStageQueueCapacity = capacity;
    metrics.inFlightBlocks =
        m_inFlightFanOutBlocks.load(std::memory_order_acquire);
    return metrics;
}

bool ProcessingEngine::running() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_running;
}

void ProcessingEngine::setWaterfallConfig(WaterfallAggregatorConfig config)
{
    std::lock_guard lock(m_waterfallMutex);
    m_waterfallAggregator.setConfig(std::move(config));
    m_nextWaterfallSourceSnapshotSequenceId = 1;
}

void ProcessingEngine::setSpectrumConfig(SpectrumAggregatorConfig config)
{
    std::lock_guard lock(m_spectrumMutex);
    m_spectrumAggregator.setConfig(std::move(config));
}

void ProcessingEngine::setBearingConfig(BearingAggregatorConfig config)
{
    std::lock_guard lock(m_bearingMutex);
    m_bearingAggregator.setConfig(std::move(config));
}

void ProcessingEngine::setSignalParameterConfig(SignalParameterAggregatorConfig config)
{
    std::lock_guard lock(m_signalParameterMutex);
    m_signalParameterAggregator.setConfig(std::move(config));
}

std::shared_ptr<const SignalParameterSnapshot> ProcessingEngine::forceSignalParameterSnapshot()
{
    std::shared_ptr<const SignalParameterSnapshot> snapshot;
    {
        std::lock_guard signalParameterLock(m_signalParameterMutex);
        snapshot = m_signalParameterAggregator.forceSnapshot();
    }

    if (snapshot && m_signalParameterSnapshots) {
        m_signalParameterSnapshots->publish(snapshot);
    }
    if (snapshot && m_metrics) {
        m_metrics->recordSignalParameterSnapshotProduced(1);
    }

    return snapshot;
}

void ProcessingEngine::workerLoop()
{
    for (;;) {
        auto block = m_queue ? m_queue->pop() : std::nullopt;
        if (!block) {
            break;
        }

        {
            std::lock_guard lock(m_mutex);
            m_processingBlock = true;
        }

        auto handle = std::move(*block);
        processBlock(std::move(handle));

        {
            std::lock_guard lock(m_mutex);
            m_processingBlock = false;
            ++m_completedQueuePops;
        }
        m_flushCondition.notify_all();
    }

    {
        std::lock_guard lock(m_mutex);
        m_running = false;
        m_processingBlock = false;
    }
    m_flushCondition.notify_all();
}

void ProcessingEngine::processBlock(SignalBlockHandle block)
{
    if (!block) {
        return;
    }

    if (m_processingConfig.processingMode == ProcessingMode::ParallelFanOut) {
        processBlockParallel(std::move(block));
        return;
    }

    processBlockSequential(*block);
}

void ProcessingEngine::processBlockSequential(const SignalBlock& block)
{
    const auto startedAt = Clock::now();
    const auto accounting = makeBlockAccounting(block, startedAt);
    processWaterfallStage(block);
    processSpectrumStage(block);
    processBearingStage(block);
    processSignalParameterStage(block);
    recordBlockCompleted(accounting, Clock::now() - startedAt);
}

void ProcessingEngine::processBlockParallel(SignalBlockHandle block)
{
    const auto startedAt = Clock::now();
    auto accounting = makeBlockAccounting(*block, startedAt);

    auto context = std::make_shared<FanOutBlockContext>();
    context->block = std::move(block);
    context->accounting = std::move(accounting);
    context->startedAt = startedAt;
    context->remainingStages.store(static_cast<int>(kFanOutStageCount),
                                   std::memory_order_release);

    m_inFlightFanOutBlocks.fetch_add(1, std::memory_order_acq_rel);
    if (!submitFanOutContext(context)) {
        m_inFlightFanOutBlocks.fetch_sub(1, std::memory_order_acq_rel);
        m_flushCondition.notify_all();
        if (m_metrics) {
            m_metrics->recordParallelFanOutRejectedBlock();
            m_metrics->recordParallelFanOutFallbackBlock();
        }
        processBlockSequential(*context->block);
        return;
    }

    if (m_metrics) {
        m_metrics->recordParallelFanOutBlock();
    }
}

ProcessingEngine::BlockAccountingData ProcessingEngine::makeBlockAccounting(
    const SignalBlock& block,
    std::chrono::steady_clock::time_point startedAt) const
{
    BlockAccountingData accounting;
    accounting.sampleCount = static_cast<std::uint64_t>(block.sampleCount());
    accounting.firstSampleIndex = block.firstSampleIndex();
    accounting.lastSampleIndex = block.lastSampleIndex();

    const auto producedAt = block.producedAt();
    accounting.blockAge = producedAt == Clock::time_point{}
        ? std::chrono::milliseconds{0}
        : std::chrono::duration_cast<std::chrono::milliseconds>(startedAt - producedAt);

    std::map<std::pair<int, int>, BandBeamAggregateSummary> aggregates;
    for (const auto& sample : block.samples()) {
        if (!accounting.hasSamples) {
            accounting.firstSampleIndex = sample.sampleIndex;
            accounting.lastSampleIndex = sample.sampleIndex;
            accounting.hasSamples = true;
        } else {
            accounting.firstSampleIndex =
                std::min(accounting.firstSampleIndex, sample.sampleIndex);
            accounting.lastSampleIndex =
                std::max(accounting.lastSampleIndex, sample.sampleIndex);
        }

        auto& summary = aggregates[{sample.bandIndex, sample.beamIndex}];
        summary.bandIndex = sample.bandIndex;
        summary.beamIndex = sample.beamIndex;
        ++summary.sampleCount;
        summary.maxAmplitude = std::max(summary.maxAmplitude, sample.amplitude);
    }

    accounting.bandBeamSummaries.reserve(aggregates.size());
    for (const auto& [key, value] : aggregates) {
        (void)key;
        accounting.bandBeamSummaries.push_back(value);
    }

    return accounting;
}

void ProcessingEngine::recordBlockCompleted(
    const BlockAccountingData& accounting,
    std::chrono::steady_clock::duration processingElapsed)
{
    {
        std::lock_guard lock(m_mutex);
        ++m_summary.processedBlocks;
        m_summary.processedSamples += accounting.sampleCount;
        if (accounting.hasSamples || accounting.sampleCount == 0) {
            if (m_summary.processedBlocks == 1) {
                m_summary.firstSampleIndex = accounting.firstSampleIndex;
                m_summary.lastSampleIndex = accounting.lastSampleIndex;
            } else {
                m_summary.firstSampleIndex =
                    std::min(m_summary.firstSampleIndex, accounting.firstSampleIndex);
                m_summary.lastSampleIndex =
                    std::max(m_summary.lastSampleIndex, accounting.lastSampleIndex);
            }
        }

        for (const auto& value : accounting.bandBeamSummaries) {
            const auto found =
                std::find_if(m_summary.bandBeamSummaries.begin(),
                             m_summary.bandBeamSummaries.end(),
                             [&value](const auto& existing) {
                                 return existing.bandIndex == value.bandIndex
                                     && existing.beamIndex == value.beamIndex;
                             });
            if (found == m_summary.bandBeamSummaries.end()) {
                m_summary.bandBeamSummaries.push_back(value);
            } else {
                found->sampleCount += value.sampleCount;
                found->maxAmplitude = std::max(found->maxAmplitude, value.maxAmplitude);
            }
        }
    }

    const auto processingLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(processingElapsed);
    if (m_metrics) {
        m_metrics->recordProcessedBlock(static_cast<std::size_t>(accounting.sampleCount),
                                        accounting.blockAge,
                                        processingElapsed);
    }
    if (m_diagnostics) {
        m_diagnostics->recordProcessingLatency(processingLatency);
    }
}

void ProcessingEngine::processWaterfallStage(const SignalBlock& block)
{
    WaterfallAggregationResult waterfallResult;
    WaterfallAggregatorConfig waterfallConfig;
    const auto waterfallAggregationStartedAt = Clock::now();
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        waterfallResult = m_waterfallAggregator.consume(block);
        waterfallConfig = m_waterfallAggregator.config();
    }
    const auto waterfallAggregationElapsed = Clock::now() - waterfallAggregationStartedAt;
    const auto waterfallAggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            waterfallAggregationElapsed);
    if (m_metrics) {
        m_metrics->recordWaterfallAggregationLatency(waterfallAggregationElapsed);
    }
    Clock::duration waterfallPublishLatency{};
    publishWaterfallRows(std::move(waterfallResult),
                         std::move(waterfallConfig),
                         waterfallAggregationLatency,
                         &waterfallPublishLatency);
    if (m_metrics) {
        m_metrics->recordWaterfallRowPublishLatency(waterfallPublishLatency);
    }
}

void ProcessingEngine::processSpectrumStage(const SignalBlock& block)
{
    SpectrumAggregationResult spectrumResult;
    const auto spectrumAggregationStartedAt = Clock::now();
    {
        std::lock_guard spectrumLock(m_spectrumMutex);
        spectrumResult = m_spectrumAggregator.consume(block);
    }
    const auto spectrumAggregationElapsed = Clock::now() - spectrumAggregationStartedAt;
    const auto spectrumAggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            spectrumAggregationElapsed);
    if (m_metrics) {
        m_metrics->recordSpectrumAggregationLatency(spectrumAggregationElapsed);
        m_metrics->recordSpectrumSampleLoopLatency(spectrumResult.timing.sampleLoop);
        m_metrics->recordSpectrumWindowCalculationLatency(
            spectrumResult.timing.windowCalculation);
        m_metrics->recordSpectrumBinCalculationLatency(
            spectrumResult.timing.binCalculation);
        m_metrics->recordSpectrumBinUpdateLatency(spectrumResult.timing.binUpdate);
        m_metrics->recordSpectrumBandSummaryUpdateLatency(
            spectrumResult.timing.bandSummaryUpdate);
        m_metrics->recordSpectrumCloseWindowLatency(spectrumResult.timing.closeWindow);
        m_metrics->recordSpectrumSnapshotBuildLatency(
            spectrumResult.timing.snapshotBuild);
        m_metrics->recordSpectrumFastPathUsage(
            spectrumResult.usedFastWindowIndex,
            spectrumResult.usedFastBinIndex,
            spectrumResult.usedFastBandSummaryStorage);
        m_metrics->recordSpectrumIncrementalWindowUsage(
            spectrumResult.usedIncrementalWindowIndex,
            spectrumResult.incrementalWindowFallbacks);
        if (spectrumResult.usedBlockLocalAccumulation) {
            m_metrics->recordSpectrumBlockLocalAccumulationBlock();
        }
    }
    Clock::duration spectrumPublishLatency{};
    publishSpectrumSnapshots(std::move(spectrumResult),
                             spectrumAggregationLatency,
                             &spectrumPublishLatency);
    if (m_metrics) {
        m_metrics->recordSpectrumSnapshotPublishLatency(spectrumPublishLatency);
    }
}

void ProcessingEngine::processBearingStage(const SignalBlock& block)
{
    BearingAggregationResult bearingResult;
    const auto bearingAggregationStartedAt = Clock::now();
    {
        std::lock_guard bearingLock(m_bearingMutex);
        bearingResult = m_bearingAggregator.consume(block);
    }
    const auto bearingAggregationElapsed = Clock::now() - bearingAggregationStartedAt;
    const auto bearingAggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            bearingAggregationElapsed);
    if (m_metrics) {
        m_metrics->recordBearingAggregationLatency(bearingAggregationElapsed);
        m_metrics->recordBearingSampleLoopLatency(bearingResult.timing.sampleLoop);
        m_metrics->recordBearingWindowCalculationLatency(
            bearingResult.timing.windowCalculation);
        m_metrics->recordBearingBinCalculationLatency(
            bearingResult.timing.binCalculation);
        m_metrics->recordBearingCandidateUpdateLatency(
            bearingResult.timing.candidateUpdate);
        m_metrics->recordBearingCloseWindowLatency(bearingResult.timing.closeWindow);
        m_metrics->recordBearingSnapshotBuildLatency(
            bearingResult.timing.snapshotBuild);
        m_metrics->recordBearingEstimateCalculationLatency(
            bearingResult.timing.estimateCalculation);
        if (bearingResult.usedFastCandidateStorage) {
            m_metrics->recordBearingFastCandidateStorageBlock();
        }
    }
    Clock::duration bearingPublishLatency{};
    publishBearingSnapshots(std::move(bearingResult),
                            bearingAggregationLatency,
                            &bearingPublishLatency);
    if (m_metrics) {
        m_metrics->recordBearingSnapshotPublishLatency(bearingPublishLatency);
    }
}

void ProcessingEngine::processSignalParameterStage(const SignalBlock& block)
{
    SignalParameterAggregationResult signalParameterResult;
    const auto signalParameterAggregationStartedAt = Clock::now();
    {
        std::lock_guard signalParameterLock(m_signalParameterMutex);
        signalParameterResult = m_signalParameterAggregator.consume(block);
    }
    const auto signalParameterAggregationElapsed =
        Clock::now() - signalParameterAggregationStartedAt;
    const auto signalParameterAggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            signalParameterAggregationElapsed);
    if (m_metrics) {
        m_metrics->recordSignalParameterAggregationLatency(signalParameterAggregationElapsed);
        m_metrics->recordSignalParameterIngestLatency(signalParameterResult.timing.ingest);
        m_metrics->recordSignalParameterSnapshotDecisionLatency(
            signalParameterResult.timing.snapshotDecision);
        m_metrics->recordSignalParameterFinalizeLatency(
            signalParameterResult.timing.finalize);
        m_metrics->recordSignalParameterSnapshotBuildLatency(
            signalParameterResult.timing.snapshotBuild);
        if (signalParameterResult.usedTrustedFixedBandFastPath) {
            m_metrics->recordSignalParameterTrustedFixedBandFastPathBlock();
        }
    }

    Clock::duration signalParameterPublishLatency{};
    publishSignalParameterSnapshots(std::move(signalParameterResult),
                                    signalParameterAggregationLatency,
                                    &signalParameterPublishLatency);
    if (m_metrics) {
        m_metrics->recordSignalParameterSnapshotPublishLatency(
            signalParameterPublishLatency);
    }
}

void ProcessingEngine::publishWaterfallRows(WaterfallAggregationResult result,
                                            WaterfallAggregatorConfig config,
                                            std::chrono::milliseconds aggregationLatency,
                                            std::chrono::steady_clock::duration* publishLatency)
{
    const auto publishStartedAt = Clock::now();
    WaterfallRowQueuePushResult pushResult;
    const bool hasRows = !result.rows.empty();
    if (hasRows && m_waterfallRows) {
        std::uint64_t sourceSnapshotSequenceId = 0;
        {
            std::lock_guard waterfallLock(m_waterfallMutex);
            sourceSnapshotSequenceId = m_nextWaterfallSourceSnapshotSequenceId++;
        }

        WaterfallRowBatchMetadata metadata;
        metadata.sourceSnapshotSequenceId = sourceSnapshotSequenceId;
        metadata.sourceMinHz = config.sourceMinHz;
        metadata.sourceMaxHz = config.sourceMaxHz;
        metadata.viewMinHz = static_cast<double>(config.sourceMinHz);
        metadata.viewMaxHz = static_cast<double>(config.sourceMaxHz);
        metadata.renderBinCount = config.renderBinCount;
        metadata.rowPeriodNs = config.rowPeriodNs;
        pushResult = m_waterfallRows->pushRows(std::move(result.rows), metadata);
    } else if (hasRows) {
        std::lock_guard waterfallLock(m_waterfallMutex);
        ++m_nextWaterfallSourceSnapshotSequenceId;
    }
    if (publishLatency) {
        *publishLatency = Clock::now() - publishStartedAt;
    }

    const std::uint64_t producedSnapshots = hasRows ? 1 : 0;
    if (m_metrics) {
        m_metrics->recordWaterfallAggregation(result.deltaCounters.producedRows,
                                              producedSnapshots,
                                              result.deltaCounters.invalidFrequencySamples,
                                              result.deltaCounters.outOfRangeSamples,
                                              result.deltaCounters.emptyBlocks,
                                              aggregationLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordWaterfallAggregation(
            result.deltaCounters.invalidFrequencySamples,
            result.deltaCounters.outOfRangeSamples,
            result.deltaCounters.emptyBlocks,
            result.deltaCounters.producedRows,
            producedSnapshots,
            aggregationLatency);
        m_diagnostics->recordWaterfallRowQueueOverflow(pushResult.droppedRows,
                                                       pushResult.depth,
                                                       pushResult.capacity);
        m_diagnostics->recordWaterfallRowTimebaseMismatch(
            pushResult.waterfallTimebaseMismatchWarnings,
            pushResult.waterfallRowUtcDeltaMinMs,
            pushResult.waterfallRowUtcDeltaMaxMs,
            pushResult.waterfallExpectedRowPeriodMs);
    }
}

void ProcessingEngine::publishSpectrumSnapshots(
    SpectrumAggregationResult result,
    std::chrono::milliseconds aggregationLatency,
    std::chrono::steady_clock::duration* publishLatency)
{
    const auto publishStartedAt = Clock::now();
    const auto producedSnapshots =
        static_cast<std::uint64_t>(result.snapshots.size());
    std::uint64_t latestSequence = 0;
    for (const auto& snapshot : result.snapshots) {
        if (!snapshot) {
            continue;
        }
        latestSequence = snapshot->sequenceId;
        if (m_spectrumSnapshots) {
            m_spectrumSnapshots->publish(snapshot);
        }
    }
    if (publishLatency) {
        *publishLatency = Clock::now() - publishStartedAt;
    }

    if (m_metrics) {
        m_metrics->recordSpectrumAggregation(producedSnapshots,
                                             latestSequence,
                                             result.deltaCounters.invalidSamples,
                                             result.deltaCounters.outOfRangeSamples,
                                             aggregationLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordSpectrumAggregation(result.deltaCounters.invalidSamples,
                                                result.deltaCounters.outOfRangeSamples,
                                                producedSnapshots,
                                                aggregationLatency);
    }
}

void ProcessingEngine::publishBearingSnapshots(
    BearingAggregationResult result,
    std::chrono::milliseconds aggregationLatency,
    std::chrono::steady_clock::duration* publishLatency)
{
    const auto publishStartedAt = Clock::now();
    const auto producedSnapshots =
        static_cast<std::uint64_t>(result.snapshots.size());
    for (const auto& snapshot : result.snapshots) {
        if (!snapshot) {
            continue;
        }
        if (m_bearingSnapshots) {
            m_bearingSnapshots->publish(snapshot);
        }
    }
    if (publishLatency) {
        *publishLatency = Clock::now() - publishStartedAt;
    }

    if (m_metrics) {
        m_metrics->recordBearingAggregation(
            producedSnapshots,
            result.deltaCounters.producedEstimates,
            result.deltaCounters.completeCandidates,
            result.deltaCounters.incompleteCandidates,
            result.deltaCounters.missingBeam0Candidates,
            result.deltaCounters.missingBeam1Candidates,
            aggregationLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordBearingAggregation(
            result.deltaCounters.completeCandidates,
            result.deltaCounters.incompleteCandidates,
            result.deltaCounters.missingBeam0Candidates,
            result.deltaCounters.missingBeam1Candidates,
            producedSnapshots,
            result.deltaCounters.producedEstimates,
            aggregationLatency);
    }
}

void ProcessingEngine::publishSignalParameterSnapshots(
    SignalParameterAggregationResult result,
    std::chrono::milliseconds aggregationLatency,
    std::chrono::steady_clock::duration* publishLatency)
{
    const auto publishStartedAt = Clock::now();
    (void)aggregationLatency;
    const std::uint64_t producedSnapshots =
        result.snapshotPublished && result.snapshot ? 1 : 0;
    if (result.snapshot && m_signalParameterSnapshots) {
        m_signalParameterSnapshots->publish(std::move(result.snapshot));
    }
    if (publishLatency) {
        *publishLatency = Clock::now() - publishStartedAt;
    }
    if (m_metrics) {
        m_metrics->recordSignalParameterSnapshotProduced(producedSnapshots);
    }
}

void ProcessingEngine::flushWaterfallRows()
{
    WaterfallAggregationResult result;
    WaterfallAggregatorConfig config;
    const auto aggregationStartedAt = Clock::now();
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        result = m_waterfallAggregator.flush();
        config = m_waterfallAggregator.config();
    }
    const auto aggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - aggregationStartedAt);
    publishWaterfallRows(std::move(result), std::move(config), aggregationLatency);
}

void ProcessingEngine::flushSpectrumSnapshots()
{
    SpectrumAggregationResult result;
    const auto aggregationStartedAt = Clock::now();
    {
        std::lock_guard spectrumLock(m_spectrumMutex);
        result = m_spectrumAggregator.flush();
    }
    const auto aggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - aggregationStartedAt);
    publishSpectrumSnapshots(std::move(result), aggregationLatency);
}

void ProcessingEngine::flushBearingSnapshots()
{
    BearingAggregationResult result;
    const auto aggregationStartedAt = Clock::now();
    {
        std::lock_guard bearingLock(m_bearingMutex);
        result = m_bearingAggregator.flush();
    }
    const auto aggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - aggregationStartedAt);
    publishBearingSnapshots(std::move(result), aggregationLatency);
}

void ProcessingEngine::startFanOutWorkers()
{
    m_stageWorkers[fanOutStageIndex(FanOutStage::Waterfall)] =
        std::thread(&ProcessingEngine::stageWorkerLoop, this, FanOutStage::Waterfall);
    m_stageWorkers[fanOutStageIndex(FanOutStage::Spectrum)] =
        std::thread(&ProcessingEngine::stageWorkerLoop, this, FanOutStage::Spectrum);
    m_stageWorkers[fanOutStageIndex(FanOutStage::Bearing)] =
        std::thread(&ProcessingEngine::stageWorkerLoop, this, FanOutStage::Bearing);
    m_stageWorkers[fanOutStageIndex(FanOutStage::SignalParameter)] =
        std::thread(&ProcessingEngine::stageWorkerLoop, this, FanOutStage::SignalParameter);
}

void ProcessingEngine::stopFanOutWorkers()
{
    {
        std::lock_guard lock(m_stageMutex);
        m_stageStopRequested = true;
    }
    m_stageCondition.notify_all();

    for (auto& worker : m_stageWorkers) {
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
            worker.join();
        }
    }
    m_flushCondition.notify_all();
}

void ProcessingEngine::resetFanOutQueues()
{
    std::lock_guard lock(m_stageMutex);
    for (auto& queue : m_stageQueues) {
        queue.clear();
    }
    m_stageStopRequested = false;
}

bool ProcessingEngine::submitFanOutContext(
    const std::shared_ptr<FanOutBlockContext>& context)
{
    if (!context) {
        return false;
    }

    const auto capacity = m_processingConfig.stageQueueCapacity;
    std::array<std::size_t, kFanOutStageCount> depths{};
    std::size_t failedStageIndex = kFanOutStageCount;
    bool failedAllStages = false;
    bool submitted = false;

    {
        std::lock_guard lock(m_stageMutex);
        if (m_stageStopRequested) {
            return false;
        }

        if (capacity == 0) {
            failedAllStages = true;
        } else {
            for (std::size_t index = 0; index < m_stageQueues.size(); ++index) {
                depths[index] = m_stageQueues[index].size();
                if (m_stageQueues[index].size() >= capacity) {
                    failedStageIndex = index;
                    break;
                }
            }

            if (failedStageIndex == kFanOutStageCount) {
                const auto enqueuedAt = Clock::now();
                for (std::size_t index = 0; index < m_stageQueues.size(); ++index) {
                    m_stageQueues[index].push_back(StageJob{context, enqueuedAt});
                    depths[index] = m_stageQueues[index].size();
                }
                submitted = true;
            }
        }
    }

    if (failedAllStages) {
        if (m_metrics) {
            for (std::size_t index = 0; index < kFanOutStageCount; ++index) {
                m_metrics->recordStageSubmitFailure(fanOutStageMetricAt(index), 0, capacity);
            }
        }
        return false;
    }

    if (failedStageIndex != kFanOutStageCount) {
        if (m_metrics) {
            m_metrics->recordStageSubmitFailure(fanOutStageMetricAt(failedStageIndex),
                                                depths[failedStageIndex],
                                                capacity);
        }
        return false;
    }

    if (!submitted) {
        return false;
    }

    if (m_metrics) {
        for (std::size_t index = 0; index < kFanOutStageCount; ++index) {
            m_metrics->recordStageEnqueued(fanOutStageMetricAt(index),
                                           depths[index],
                                           capacity);
        }
    }
    m_stageCondition.notify_all();
    return true;
}

ProcessingEngine::StageJob
ProcessingEngine::popFanOutStageJob(FanOutStage stage)
{
    const auto index = fanOutStageIndex(stage);
    std::unique_lock lock(m_stageMutex);
    m_stageCondition.wait(lock, [this, index] {
        return m_stageStopRequested || !m_stageQueues[index].empty();
    });

    if (m_stageQueues[index].empty()) {
        return {};
    }

    auto job = std::move(m_stageQueues[index].front());
    m_stageQueues[index].pop_front();
    job.queueDepthAfterPop = m_stageQueues[index].size();
    job.queueCapacity = m_processingConfig.stageQueueCapacity;
    lock.unlock();
    m_flushCondition.notify_all();
    return job;
}

void ProcessingEngine::stageWorkerLoop(FanOutStage stage)
{
    for (;;) {
        auto job = popFanOutStageJob(stage);
        if (!job.context) {
            break;
        }

        const auto startedAt = Clock::now();
        if (m_metrics) {
            m_metrics->recordStageStarted(fanOutStageMetric(stage),
                                          startedAt - job.enqueuedAt,
                                          job.queueDepthAfterPop,
                                          job.queueCapacity);
        }

        const auto serviceStartedAt = Clock::now();
        processFanOutStage(stage, *job.context->block);
        const auto serviceElapsed = Clock::now() - serviceStartedAt;
        if (m_metrics) {
            m_metrics->recordStageCompleted(fanOutStageMetric(stage),
                                            serviceElapsed,
                                            job.context->block->sampleCount());
        }
        completeFanOutStage(job.context);
    }
}

void ProcessingEngine::processFanOutStage(FanOutStage stage, const SignalBlock& block)
{
    switch (stage) {
    case FanOutStage::Waterfall:
        processWaterfallStage(block);
        break;
    case FanOutStage::Spectrum:
        processSpectrumStage(block);
        break;
    case FanOutStage::Bearing:
        processBearingStage(block);
        break;
    case FanOutStage::SignalParameter:
        processSignalParameterStage(block);
        break;
    }
}

void ProcessingEngine::completeFanOutStage(
    const std::shared_ptr<FanOutBlockContext>& context)
{
    if (!context) {
        return;
    }

    if (context->remainingStages.fetch_sub(1, std::memory_order_acq_rel) != 1) {
        return;
    }

    const auto elapsed = Clock::now() - context->startedAt;
    recordBlockCompleted(context->accounting, elapsed);
    if (m_metrics) {
        m_metrics->recordParallelFanOutEndToEndLatency(elapsed);
    }
    m_inFlightFanOutBlocks.fetch_sub(1, std::memory_order_acq_rel);
    m_flushCondition.notify_all();
}

bool ProcessingEngine::fanOutDrained() const
{
    if (m_inFlightFanOutBlocks.load(std::memory_order_acquire) != 0) {
        return false;
    }

    std::lock_guard lock(m_stageMutex);
    return std::all_of(m_stageQueues.begin(), m_stageQueues.end(), [](const auto& queue) {
        return queue.empty();
    });
}

bool ProcessingEngine::stageWorkersJoinable() const noexcept
{
    return std::any_of(m_stageWorkers.begin(), m_stageWorkers.end(), [](const auto& worker) {
        return worker.joinable();
    });
}

std::size_t ProcessingEngine::fanOutStageIndex(FanOutStage stage) noexcept
{
    switch (stage) {
    case FanOutStage::Waterfall:
        return 0;
    case FanOutStage::Spectrum:
        return 1;
    case FanOutStage::Bearing:
        return 2;
    case FanOutStage::SignalParameter:
        return 3;
    }

    return 0;
}

PipelineStageMetric ProcessingEngine::fanOutStageMetric(FanOutStage stage) noexcept
{
    return fanOutStageMetricAt(fanOutStageIndex(stage));
}

PipelineStageMetric ProcessingEngine::fanOutStageMetricAt(std::size_t index) noexcept
{
    switch (index) {
    case 0:
        return PipelineStageMetric::Waterfall;
    case 1:
        return PipelineStageMetric::Spectrum;
    case 2:
        return PipelineStageMetric::Bearing;
    case 3:
        return PipelineStageMetric::SignalParameter;
    default:
        return PipelineStageMetric::Waterfall;
    }
}

} // namespace siriusscope::pipeline
