#include "pipeline/pipeline_metrics.h"

#include "core/domain_models.h"

#include <algorithm>

namespace siriusscope::pipeline {

void PipelineMetrics::reset()
{
    std::lock_guard lock(m_mutex);
    m_startedAt = std::chrono::steady_clock::now();
    m_inputBlocks = 0;
    m_inputSamples = 0;
    m_processedBlocks = 0;
    m_processedSamples = 0;
    m_droppedBlocks = 0;
    m_droppedSamples = 0;
    m_producedWaterfallRows = 0;
    m_producedWaterfallSnapshots = 0;
    m_waterfallInvalidFrequencySamples = 0;
    m_waterfallOutOfRangeSamples = 0;
    m_waterfallEmptyBlocks = 0;
    m_producedSpectrumSnapshots = 0;
    m_latestSpectrumSnapshotSequence = 0;
    m_spectrumInvalidSamples = 0;
    m_spectrumOutOfRangeSamples = 0;
    m_producedBearingSnapshots = 0;
    m_producedBearingEstimates = 0;
    m_completeBearingCandidates = 0;
    m_incompleteBearingCandidates = 0;
    m_missingBeam0Candidates = 0;
    m_missingBeam1Candidates = 0;
    m_producedSignalParameterSnapshots = 0;
    m_signalParameterTrustedFixedBandFastPathBlocks = 0;
    m_parallelFanOutBlocks = 0;
    m_parallelFanOutFallbackBlocks = 0;
    m_parallelFanOutRejectedBlocks = 0;
    m_stageMetrics = {};
    m_bearingFastCandidateStorageBlocks = 0;
    m_bearingBlockLocalAccumulationBlocks = 0;
    m_spectrumFastWindowBlocks = 0;
    m_spectrumFastBinBlocks = 0;
    m_spectrumFastBandSummaryBlocks = 0;
    m_spectrumIncrementalWindowBlocks = 0;
    m_spectrumIncrementalWindowFallbacks = 0;
    m_spectrumBlockLocalAccumulationBlocks = 0;
    m_rxLatencyMax = std::chrono::milliseconds{0};
    m_processingLatencyMax = std::chrono::milliseconds{0};
    m_aggregationLatencyMax = std::chrono::milliseconds{0};
    m_spectrumAggregationLatencyMax = std::chrono::milliseconds{0};
    m_bearingAggregationLatencyMax = std::chrono::milliseconds{0};
    m_maxBlockAge = std::chrono::milliseconds{0};
    m_processBlockLatency = {};
    m_waterfallAggregationLatency = {};
    m_spectrumAggregationLatency = {};
    m_spectrumSampleLoopLatency = {};
    m_spectrumWindowCalculationLatency = {};
    m_spectrumBinCalculationLatency = {};
    m_spectrumBinUpdateLatency = {};
    m_spectrumBandSummaryUpdateLatency = {};
    m_spectrumCloseWindowLatency = {};
    m_spectrumSnapshotBuildLatency = {};
    m_bearingAggregationLatency = {};
    m_bearingSampleLoopLatency = {};
    m_bearingWindowCalculationLatency = {};
    m_bearingBinCalculationLatency = {};
    m_bearingCandidateUpdateLatency = {};
    m_bearingCloseWindowLatency = {};
    m_bearingSnapshotBuildLatency = {};
    m_bearingEstimateCalculationLatency = {};
    m_signalParameterAggregationLatency = {};
    m_signalParameterIngestLatency = {};
    m_signalParameterSnapshotDecisionLatency = {};
    m_signalParameterFinalizeLatency = {};
    m_signalParameterSnapshotBuildLatency = {};
    m_parallelFanOutEndToEndLatency = {};
    m_waterfallRowPublishLatency = {};
    m_spectrumSnapshotPublishLatency = {};
    m_bearingSnapshotPublishLatency = {};
    m_signalParameterSnapshotPublishLatency = {};
    m_processedBlockSamplesTotal = 0;
}

void PipelineMetrics::recordInputBlock(std::size_t sampleCount,
                                       std::chrono::steady_clock::time_point producedAt)
{
    const auto now = std::chrono::steady_clock::now();
    const auto latency = producedAt == std::chrono::steady_clock::time_point{}
        ? std::chrono::milliseconds{0}
        : std::chrono::duration_cast<std::chrono::milliseconds>(now - producedAt);

    std::lock_guard lock(m_mutex);
    ++m_inputBlocks;
    m_inputSamples += static_cast<std::uint64_t>(sampleCount);
    m_rxLatencyMax = std::max(m_rxLatencyMax, latency);
}

void PipelineMetrics::recordDroppedBlock(std::size_t sampleCount)
{
    std::lock_guard lock(m_mutex);
    ++m_droppedBlocks;
    m_droppedSamples += static_cast<std::uint64_t>(sampleCount);
}

void PipelineMetrics::recordProcessedBlock(std::size_t sampleCount,
                                           std::chrono::milliseconds blockAge,
                                           std::chrono::steady_clock::duration processingLatency)
{
    const auto processingLatencyMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(processingLatency);
    std::lock_guard lock(m_mutex);
    ++m_processedBlocks;
    m_processedSamples += static_cast<std::uint64_t>(sampleCount);
    m_maxBlockAge = std::max(m_maxBlockAge, blockAge);
    m_processingLatencyMax = std::max(m_processingLatencyMax, processingLatencyMs);
    recordLatency(m_processBlockLatency, millisecondsFor(processingLatency));
    m_processedBlockSamplesTotal += static_cast<std::uint64_t>(sampleCount);
}

void PipelineMetrics::recordProcessBlockLatency(std::chrono::steady_clock::duration elapsed,
                                                std::size_t sampleCount)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_processBlockLatency, millisecondsFor(elapsed));
    m_processedBlockSamplesTotal += static_cast<std::uint64_t>(sampleCount);
}

void PipelineMetrics::recordWaterfallAggregationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_waterfallAggregationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumAggregationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumAggregationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumSampleLoopLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumSampleLoopLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumWindowCalculationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumWindowCalculationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumBinCalculationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumBinCalculationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumBinUpdateLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumBinUpdateLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumBandSummaryUpdateLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumBandSummaryUpdateLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumCloseWindowLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumCloseWindowLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumSnapshotBuildLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumSnapshotBuildLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingAggregationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingAggregationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingSampleLoopLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingSampleLoopLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingWindowCalculationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingWindowCalculationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingBinCalculationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingBinCalculationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingCandidateUpdateLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingCandidateUpdateLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingCloseWindowLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingCloseWindowLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingSnapshotBuildLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingSnapshotBuildLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingEstimateCalculationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingEstimateCalculationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSignalParameterAggregationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_signalParameterAggregationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSignalParameterIngestLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_signalParameterIngestLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSignalParameterSnapshotDecisionLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_signalParameterSnapshotDecisionLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSignalParameterFinalizeLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_signalParameterFinalizeLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSignalParameterSnapshotBuildLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_signalParameterSnapshotBuildLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordWaterfallRowPublishLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_waterfallRowPublishLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSpectrumSnapshotPublishLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_spectrumSnapshotPublishLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingSnapshotPublishLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingSnapshotPublishLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSignalParameterSnapshotPublishLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_signalParameterSnapshotPublishLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordWaterfallAggregation(
    std::uint64_t producedRows,
    std::uint64_t producedSnapshots,
    std::uint64_t invalidFrequencySamples,
    std::uint64_t outOfRangeSamples,
    std::uint64_t emptyBlocks,
    std::chrono::milliseconds aggregationLatency)
{
    std::lock_guard lock(m_mutex);
    m_producedWaterfallRows += producedRows;
    m_producedWaterfallSnapshots += producedSnapshots;
    m_waterfallInvalidFrequencySamples += invalidFrequencySamples;
    m_waterfallOutOfRangeSamples += outOfRangeSamples;
    m_waterfallEmptyBlocks += emptyBlocks;
    m_aggregationLatencyMax = std::max(m_aggregationLatencyMax, aggregationLatency);
}

void PipelineMetrics::recordSpectrumAggregation(
    std::uint64_t producedSnapshots,
    std::uint64_t latestSnapshotSequence,
    std::uint64_t invalidSamples,
    std::uint64_t outOfRangeSamples,
    std::chrono::milliseconds aggregationLatency)
{
    std::lock_guard lock(m_mutex);
    m_producedSpectrumSnapshots += producedSnapshots;
    if (latestSnapshotSequence > 0) {
        m_latestSpectrumSnapshotSequence = latestSnapshotSequence;
    }
    m_spectrumInvalidSamples += invalidSamples;
    m_spectrumOutOfRangeSamples += outOfRangeSamples;
    m_spectrumAggregationLatencyMax =
        std::max(m_spectrumAggregationLatencyMax, aggregationLatency);
}

void PipelineMetrics::recordBearingAggregation(
    std::uint64_t producedSnapshots,
    std::uint64_t producedEstimates,
    std::uint64_t completeCandidates,
    std::uint64_t incompleteCandidates,
    std::uint64_t missingBeam0Candidates,
    std::uint64_t missingBeam1Candidates,
    std::chrono::milliseconds aggregationLatency)
{
    std::lock_guard lock(m_mutex);
    m_producedBearingSnapshots += producedSnapshots;
    m_producedBearingEstimates += producedEstimates;
    m_completeBearingCandidates += completeCandidates;
    m_incompleteBearingCandidates += incompleteCandidates;
    m_missingBeam0Candidates += missingBeam0Candidates;
    m_missingBeam1Candidates += missingBeam1Candidates;
    m_bearingAggregationLatencyMax =
        std::max(m_bearingAggregationLatencyMax, aggregationLatency);
}

void PipelineMetrics::recordSignalParameterSnapshotProduced(
    std::uint64_t producedSnapshots)
{
    std::lock_guard lock(m_mutex);
    m_producedSignalParameterSnapshots += producedSnapshots;
}

void PipelineMetrics::recordSignalParameterTrustedFixedBandFastPathBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_signalParameterTrustedFixedBandFastPathBlocks;
}

void PipelineMetrics::recordParallelFanOutBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_parallelFanOutBlocks;
}

void PipelineMetrics::recordParallelFanOutFallbackBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_parallelFanOutFallbackBlocks;
}

void PipelineMetrics::recordParallelFanOutRejectedBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_parallelFanOutRejectedBlocks;
}

void PipelineMetrics::recordStageEnqueued(PipelineStageMetric stage,
                                          std::size_t queueDepth,
                                          std::size_t capacity)
{
    std::lock_guard lock(m_mutex);
    auto& stageMetrics = m_stageMetrics[stageMetricIndex(stage)];
    ++stageMetrics.enqueuedBlocks;
    stageMetrics.queueDepth = queueDepth;
    stageMetrics.queueCapacity = capacity;
    stageMetrics.queueMaxDepth = std::max(stageMetrics.queueMaxDepth, queueDepth);
}

void PipelineMetrics::recordStageSubmitFailure(PipelineStageMetric stage,
                                               std::size_t queueDepth,
                                               std::size_t capacity)
{
    std::lock_guard lock(m_mutex);
    auto& stageMetrics = m_stageMetrics[stageMetricIndex(stage)];
    ++stageMetrics.submitFailures;
    stageMetrics.queueDepth = queueDepth;
    stageMetrics.queueCapacity = capacity;
    stageMetrics.queueMaxDepth = std::max(stageMetrics.queueMaxDepth, queueDepth);
}

void PipelineMetrics::recordStageStarted(PipelineStageMetric stage,
                                         std::chrono::steady_clock::duration queueWait,
                                         std::size_t queueDepth,
                                         std::size_t capacity)
{
    std::lock_guard lock(m_mutex);
    auto& stageMetrics = m_stageMetrics[stageMetricIndex(stage)];
    ++stageMetrics.dequeuedBlocks;
    stageMetrics.queueDepth = queueDepth;
    stageMetrics.queueCapacity = capacity;
    stageMetrics.queueMaxDepth = std::max(stageMetrics.queueMaxDepth, queueDepth);
    recordLatency(stageMetrics.queueWaitLatency, millisecondsFor(queueWait));
}

void PipelineMetrics::recordStageCompleted(PipelineStageMetric stage,
                                           std::chrono::steady_clock::duration serviceLatency,
                                           std::size_t sampleCount)
{
    std::lock_guard lock(m_mutex);
    auto& stageMetrics = m_stageMetrics[stageMetricIndex(stage)];
    ++stageMetrics.processedBlocks;
    stageMetrics.processedSamples += static_cast<std::uint64_t>(sampleCount);
    recordLatency(stageMetrics.serviceLatency, millisecondsFor(serviceLatency));
}

void PipelineMetrics::recordStageQueueDepth(PipelineStageMetric stage,
                                            std::size_t queueDepth,
                                            std::size_t capacity)
{
    std::lock_guard lock(m_mutex);
    auto& stageMetrics = m_stageMetrics[stageMetricIndex(stage)];
    stageMetrics.queueDepth = queueDepth;
    stageMetrics.queueCapacity = capacity;
    stageMetrics.queueMaxDepth = std::max(stageMetrics.queueMaxDepth, queueDepth);
}

void PipelineMetrics::recordStageDroppedByPolicy(PipelineStageMetric stage,
                                                 std::size_t count)
{
    std::lock_guard lock(m_mutex);
    auto& stageMetrics = m_stageMetrics[stageMetricIndex(stage)];
    const auto value = static_cast<std::uint64_t>(count);
    stageMetrics.droppedByOverloadPolicy += value;
    stageMetrics.skippedBlocks += value;
}

void PipelineMetrics::recordStageCoalescedByPolicy(PipelineStageMetric stage,
                                                   std::size_t count)
{
    std::lock_guard lock(m_mutex);
    auto& stageMetrics = m_stageMetrics[stageMetricIndex(stage)];
    const auto value = static_cast<std::uint64_t>(count);
    stageMetrics.coalescedByOverloadPolicy += value;
    stageMetrics.skippedBlocks += value;
}

void PipelineMetrics::recordStageSkipped(PipelineStageMetric stage,
                                         StageSkipReason reason)
{
    switch (reason) {
    case StageSkipReason::DroppedByOverloadPolicy:
        recordStageDroppedByPolicy(stage, 1);
        break;
    case StageSkipReason::ReplacedByLatest:
        recordStageCoalescedByPolicy(stage, 1);
        break;
    }
}

void PipelineMetrics::recordWaterfallStageProcessedBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_stageMetrics[stageMetricIndex(PipelineStageMetric::Waterfall)].processedBlocks;
}

void PipelineMetrics::recordSpectrumStageProcessedBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_stageMetrics[stageMetricIndex(PipelineStageMetric::Spectrum)].processedBlocks;
}

void PipelineMetrics::recordBearingStageProcessedBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_stageMetrics[stageMetricIndex(PipelineStageMetric::Bearing)].processedBlocks;
}

void PipelineMetrics::recordSignalParameterStageProcessedBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_stageMetrics[stageMetricIndex(PipelineStageMetric::SignalParameter)].processedBlocks;
}

void PipelineMetrics::recordParallelFanOutEndToEndLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_parallelFanOutEndToEndLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordBearingFastCandidateStorageBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_bearingFastCandidateStorageBlocks;
}

void PipelineMetrics::recordBearingBlockLocalAccumulationBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_bearingBlockLocalAccumulationBlocks;
}

void PipelineMetrics::recordSpectrumFastPathUsage(bool fastWindow,
                                                  bool fastBin,
                                                  bool fastBandSummary)
{
    std::lock_guard lock(m_mutex);
    if (fastWindow) {
        ++m_spectrumFastWindowBlocks;
    }
    if (fastBin) {
        ++m_spectrumFastBinBlocks;
    }
    if (fastBandSummary) {
        ++m_spectrumFastBandSummaryBlocks;
    }
}

void PipelineMetrics::recordSpectrumIncrementalWindowUsage(
    bool incrementalWindow,
    std::uint64_t fallbackCount)
{
    std::lock_guard lock(m_mutex);
    if (incrementalWindow) {
        ++m_spectrumIncrementalWindowBlocks;
    }
    m_spectrumIncrementalWindowFallbacks += fallbackCount;
}

void PipelineMetrics::recordSpectrumBlockLocalAccumulationBlock()
{
    std::lock_guard lock(m_mutex);
    ++m_spectrumBlockLocalAccumulationBlocks;
}

PipelineMetricsSnapshot PipelineMetrics::snapshot(
    const BoundedBlockQueueMetrics& queueMetrics,
    const SignalBlockPoolCounters& poolCounters,
    const WaterfallRowQueueMetrics& waterfallRowMetrics,
    const ParallelFanOutQueueMetrics& parallelFanOutQueueMetrics) const
{
    std::lock_guard lock(m_mutex);
    const auto elapsedSeconds =
        std::max(0.001, std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - m_startedAt)
                            .count());

    PipelineMetricsSnapshot snapshot;
    snapshot.inputBlocks = m_inputBlocks;
    snapshot.inputSamples = m_inputSamples;
    snapshot.processedBlocks = m_processedBlocks;
    snapshot.processedSamples = m_processedSamples;
    snapshot.droppedBlocks = m_droppedBlocks;
    snapshot.droppedSamples = m_droppedSamples;
    snapshot.inputMegabytesPerSecond = megabytesForSamples(m_inputSamples) / elapsedSeconds;
    snapshot.processedMegabytesPerSecond =
        megabytesForSamples(m_processedSamples) / elapsedSeconds;
    snapshot.rxLatencyMaxMs = static_cast<double>(m_rxLatencyMax.count());
    snapshot.processingLatencyMaxMs = static_cast<double>(m_processingLatencyMax.count());
    snapshot.maxBlockAgeMs = static_cast<double>(m_maxBlockAge.count());
    snapshot.processBlockLatency = m_processBlockLatency;
    snapshot.waterfallAggregationLatency = m_waterfallAggregationLatency;
    snapshot.spectrumAggregationLatency = m_spectrumAggregationLatency;
    snapshot.spectrumSampleLoopLatency = m_spectrumSampleLoopLatency;
    snapshot.spectrumWindowCalculationLatency = m_spectrumWindowCalculationLatency;
    snapshot.spectrumBinCalculationLatency = m_spectrumBinCalculationLatency;
    snapshot.spectrumBinUpdateLatency = m_spectrumBinUpdateLatency;
    snapshot.spectrumBandSummaryUpdateLatency = m_spectrumBandSummaryUpdateLatency;
    snapshot.spectrumCloseWindowLatency = m_spectrumCloseWindowLatency;
    snapshot.spectrumSnapshotBuildLatency = m_spectrumSnapshotBuildLatency;
    snapshot.bearingAggregationLatency = m_bearingAggregationLatency;
    snapshot.bearingSampleLoopLatency = m_bearingSampleLoopLatency;
    snapshot.bearingWindowCalculationLatency = m_bearingWindowCalculationLatency;
    snapshot.bearingBinCalculationLatency = m_bearingBinCalculationLatency;
    snapshot.bearingCandidateUpdateLatency = m_bearingCandidateUpdateLatency;
    snapshot.bearingCloseWindowLatency = m_bearingCloseWindowLatency;
    snapshot.bearingSnapshotBuildLatency = m_bearingSnapshotBuildLatency;
    snapshot.bearingEstimateCalculationLatency = m_bearingEstimateCalculationLatency;
    snapshot.signalParameterAggregationLatency = m_signalParameterAggregationLatency;
    snapshot.signalParameterIngestLatency = m_signalParameterIngestLatency;
    snapshot.signalParameterSnapshotDecisionLatency =
        m_signalParameterSnapshotDecisionLatency;
    snapshot.signalParameterFinalizeLatency = m_signalParameterFinalizeLatency;
    snapshot.signalParameterSnapshotBuildLatency = m_signalParameterSnapshotBuildLatency;
    snapshot.parallelFanOutEndToEndLatency = m_parallelFanOutEndToEndLatency;
    snapshot.waterfallRowPublishLatency = m_waterfallRowPublishLatency;
    snapshot.spectrumSnapshotPublishLatency = m_spectrumSnapshotPublishLatency;
    snapshot.bearingSnapshotPublishLatency = m_bearingSnapshotPublishLatency;
    snapshot.signalParameterSnapshotPublishLatency = m_signalParameterSnapshotPublishLatency;
    snapshot.processedBlockSamplesTotal = m_processedBlockSamplesTotal;
    snapshot.averageSamplesPerProcessedBlock = m_processBlockLatency.count == 0
        ? 0.0
        : static_cast<double>(m_processedBlockSamplesTotal)
            / static_cast<double>(m_processBlockLatency.count);
    snapshot.producedWaterfallRows = m_producedWaterfallRows;
    snapshot.producedWaterfallSnapshots = m_producedWaterfallSnapshots;
    snapshot.waterfallQueuedRows = waterfallRowMetrics.pushedRows;
    snapshot.waterfallDrainedRows = waterfallRowMetrics.drainedRows;
    snapshot.waterfallDroppedRows = waterfallRowMetrics.droppedRows;
    snapshot.waterfallRowQueueDepth = waterfallRowMetrics.depth;
    snapshot.waterfallRowQueueCapacity = waterfallRowMetrics.capacity;
    snapshot.latestWaterfallRowSequenceId = waterfallRowMetrics.latestRowSequenceId;
    snapshot.waterfallRowUtcDeltaMinMs = waterfallRowMetrics.waterfallRowUtcDeltaMinMs;
    snapshot.waterfallRowUtcDeltaMaxMs = waterfallRowMetrics.waterfallRowUtcDeltaMaxMs;
    snapshot.waterfallExpectedRowPeriodMs = waterfallRowMetrics.waterfallExpectedRowPeriodMs;
    snapshot.waterfallTimebaseMismatchWarnings =
        waterfallRowMetrics.waterfallTimebaseMismatchWarnings;
    snapshot.aggregationLatencyMaxMs = static_cast<double>(m_aggregationLatencyMax.count());
    snapshot.waterfallInvalidFrequencySamples = m_waterfallInvalidFrequencySamples;
    snapshot.waterfallOutOfRangeSamples = m_waterfallOutOfRangeSamples;
    snapshot.waterfallEmptyBlocks = m_waterfallEmptyBlocks;
    snapshot.producedSpectrumSnapshots = m_producedSpectrumSnapshots;
    snapshot.latestSpectrumSnapshotSequence = m_latestSpectrumSnapshotSequence;
    snapshot.spectrumInvalidSamples = m_spectrumInvalidSamples;
    snapshot.spectrumOutOfRangeSamples = m_spectrumOutOfRangeSamples;
    snapshot.spectrumAggregationLatencyMaxMs =
        static_cast<double>(m_spectrumAggregationLatencyMax.count());
    snapshot.spectrumFastWindowBlocks = m_spectrumFastWindowBlocks;
    snapshot.spectrumFastBinBlocks = m_spectrumFastBinBlocks;
    snapshot.spectrumFastBandSummaryBlocks = m_spectrumFastBandSummaryBlocks;
    snapshot.spectrumIncrementalWindowBlocks = m_spectrumIncrementalWindowBlocks;
    snapshot.spectrumIncrementalWindowFallbacks =
        m_spectrumIncrementalWindowFallbacks;
    snapshot.spectrumBlockLocalAccumulationBlocks =
        m_spectrumBlockLocalAccumulationBlocks;
    snapshot.producedBearingSnapshots = m_producedBearingSnapshots;
    snapshot.producedBearingEstimates = m_producedBearingEstimates;
    snapshot.completeBearingCandidates = m_completeBearingCandidates;
    snapshot.incompleteBearingCandidates = m_incompleteBearingCandidates;
    snapshot.missingBeam0Candidates = m_missingBeam0Candidates;
    snapshot.missingBeam1Candidates = m_missingBeam1Candidates;
    snapshot.bearingAggregationLatencyMaxMs =
        static_cast<double>(m_bearingAggregationLatencyMax.count());
    snapshot.bearingFastCandidateStorageBlocks = m_bearingFastCandidateStorageBlocks;
    snapshot.bearingBlockLocalAccumulationBlocks =
        m_bearingBlockLocalAccumulationBlocks;
    snapshot.producedSignalParameterSnapshots = m_producedSignalParameterSnapshots;
    snapshot.signalParameterTrustedFixedBandFastPathBlocks =
        m_signalParameterTrustedFixedBandFastPathBlocks;
    snapshot.parallelFanOutBlocks = m_parallelFanOutBlocks;
    snapshot.parallelFanOutFallbackBlocks = m_parallelFanOutFallbackBlocks;
    snapshot.parallelFanOutRejectedBlocks = m_parallelFanOutRejectedBlocks;
    snapshot.waterfallStage = m_stageMetrics[stageMetricIndex(PipelineStageMetric::Waterfall)];
    snapshot.spectrumStage = m_stageMetrics[stageMetricIndex(PipelineStageMetric::Spectrum)];
    snapshot.bearingStage = m_stageMetrics[stageMetricIndex(PipelineStageMetric::Bearing)];
    snapshot.signalParameterStage =
        m_stageMetrics[stageMetricIndex(PipelineStageMetric::SignalParameter)];
    snapshot.waterfallStageDroppedByPolicy =
        snapshot.waterfallStage.droppedByOverloadPolicy;
    snapshot.spectrumStageDroppedByPolicy =
        snapshot.spectrumStage.droppedByOverloadPolicy;
    snapshot.bearingStageDroppedByPolicy =
        snapshot.bearingStage.droppedByOverloadPolicy;
    snapshot.signalParameterStageDroppedByPolicy =
        snapshot.signalParameterStage.droppedByOverloadPolicy;
    snapshot.waterfallStageCoalescedByPolicy =
        snapshot.waterfallStage.coalescedByOverloadPolicy;
    snapshot.spectrumStageCoalescedByPolicy =
        snapshot.spectrumStage.coalescedByOverloadPolicy;
    snapshot.bearingStageCoalescedByPolicy =
        snapshot.bearingStage.coalescedByOverloadPolicy;
    snapshot.signalParameterStageCoalescedByPolicy =
        snapshot.signalParameterStage.coalescedByOverloadPolicy;
    snapshot.waterfallStageSkippedBlocks = snapshot.waterfallStage.skippedBlocks;
    snapshot.spectrumStageSkippedBlocks = snapshot.spectrumStage.skippedBlocks;
    snapshot.bearingStageSkippedBlocks = snapshot.bearingStage.skippedBlocks;
    snapshot.signalParameterStageSkippedBlocks =
        snapshot.signalParameterStage.skippedBlocks;
    snapshot.visualStageDroppedBlocks =
        snapshot.waterfallStageDroppedByPolicy
        + snapshot.spectrumStageDroppedByPolicy
        + snapshot.bearingStageDroppedByPolicy;
    snapshot.visualStageCoalescedBlocks =
        snapshot.waterfallStageCoalescedByPolicy
        + snapshot.spectrumStageCoalescedByPolicy
        + snapshot.bearingStageCoalescedByPolicy;
    const auto applyLiveQueueMetrics = [](StageMetricsSnapshot& stage,
                                          std::size_t depth,
                                          std::size_t capacity) {
        if (depth > 0 || capacity > 0) {
            stage.queueDepth = depth;
            stage.queueCapacity = capacity;
            stage.queueMaxDepth = std::max(stage.queueMaxDepth, depth);
        }
    };
    applyLiveQueueMetrics(snapshot.waterfallStage,
                          parallelFanOutQueueMetrics.waterfallStageQueueDepth,
                          parallelFanOutQueueMetrics.waterfallStageQueueCapacity);
    applyLiveQueueMetrics(snapshot.spectrumStage,
                          parallelFanOutQueueMetrics.spectrumStageQueueDepth,
                          parallelFanOutQueueMetrics.spectrumStageQueueCapacity);
    applyLiveQueueMetrics(snapshot.bearingStage,
                          parallelFanOutQueueMetrics.bearingStageQueueDepth,
                          parallelFanOutQueueMetrics.bearingStageQueueCapacity);
    applyLiveQueueMetrics(snapshot.signalParameterStage,
                          parallelFanOutQueueMetrics.signalParameterStageQueueDepth,
                          parallelFanOutQueueMetrics.signalParameterStageQueueCapacity);
    snapshot.waterfallStageQueueDepth = snapshot.waterfallStage.queueDepth;
    snapshot.spectrumStageQueueDepth = snapshot.spectrumStage.queueDepth;
    snapshot.bearingStageQueueDepth = snapshot.bearingStage.queueDepth;
    snapshot.signalParameterStageQueueDepth = snapshot.signalParameterStage.queueDepth;
    snapshot.parallelFanOutInFlightBlocks =
        parallelFanOutQueueMetrics.inFlightBlocks;
    snapshot.waterfallStageProcessedBlocks = snapshot.waterfallStage.processedBlocks;
    snapshot.spectrumStageProcessedBlocks = snapshot.spectrumStage.processedBlocks;
    snapshot.bearingStageProcessedBlocks = snapshot.bearingStage.processedBlocks;
    snapshot.signalParameterStageProcessedBlocks =
        snapshot.signalParameterStage.processedBlocks;
    snapshot.queueDepth = queueMetrics.depth;
    snapshot.queueCapacity = queueMetrics.capacity;
    snapshot.queuePushedBlocks = queueMetrics.pushedBlocks;
    snapshot.queuePoppedBlocks = queueMetrics.poppedBlocks;
    snapshot.queueDroppedBlocks = queueMetrics.droppedBlocks;
    snapshot.blockPoolCapacity = poolCounters.capacity;
    snapshot.blockPoolAvailable = poolCounters.available;
    snapshot.blockPoolInUse = poolCounters.inUse;
    snapshot.blockPoolUsage = poolCounters.capacity == 0
        ? 0.0
        : static_cast<double>(poolCounters.inUse) / static_cast<double>(poolCounters.capacity);
    snapshot.blockPoolAcquired = poolCounters.acquired;
    snapshot.blockPoolReleased = poolCounters.released;
    snapshot.blockPoolExhausted = poolCounters.exhausted;
    return snapshot;
}

double PipelineMetrics::megabytesForSamples(std::uint64_t sampleCount)
{
    return static_cast<double>(sampleCount) * static_cast<double>(sizeof(core::SignalSample))
        / (1024.0 * 1024.0);
}

double PipelineMetrics::millisecondsFor(std::chrono::steady_clock::duration elapsed)
{
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

void PipelineMetrics::recordLatency(LatencyStats& stats, double elapsedMs)
{
    ++stats.count;
    stats.totalMs += elapsedMs;
    stats.maxMs = std::max(stats.maxMs, elapsedMs);
}

std::size_t PipelineMetrics::stageMetricIndex(PipelineStageMetric stage) noexcept
{
    switch (stage) {
    case PipelineStageMetric::Waterfall:
        return 0;
    case PipelineStageMetric::Spectrum:
        return 1;
    case PipelineStageMetric::Bearing:
        return 2;
    case PipelineStageMetric::SignalParameter:
        return 3;
    }

    return 0;
}

} // namespace siriusscope::pipeline
