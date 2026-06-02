#pragma once

#include "pipeline/bounded_block_queue.h"
#include "pipeline/signal_block_pool.h"
#include "pipeline/waterfall_row_queue.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace siriusscope::pipeline {

struct LatencyStats
{
    std::uint64_t count = 0;
    double totalMs = 0.0;
    double maxMs = 0.0;

    double averageMs() const noexcept
    {
        return count == 0 ? 0.0 : totalMs / static_cast<double>(count);
    }
};

enum class PipelineStageMetric
{
    Waterfall,
    Spectrum,
    Bearing,
    SignalParameter,
};

enum class StageSkipReason
{
    DroppedByOverloadPolicy,
    ReplacedByLatest,
};

struct StageMetricsSnapshot
{
    std::uint64_t enqueuedBlocks = 0;
    std::uint64_t dequeuedBlocks = 0;
    std::uint64_t processedBlocks = 0;
    std::uint64_t processedSamples = 0;
    std::uint64_t submitFailures = 0;
    std::uint64_t droppedByOverloadPolicy = 0;
    std::uint64_t coalescedByOverloadPolicy = 0;
    std::uint64_t skippedBlocks = 0;
    std::size_t queueDepth = 0;
    std::size_t queueMaxDepth = 0;
    std::size_t queueCapacity = 0;
    LatencyStats queueWaitLatency;
    LatencyStats serviceLatency;
};

struct ParallelFanOutQueueMetrics
{
    std::size_t waterfallStageQueueDepth = 0;
    std::size_t waterfallStageQueueCapacity = 0;
    std::size_t spectrumStageQueueDepth = 0;
    std::size_t spectrumStageQueueCapacity = 0;
    std::size_t bearingStageQueueDepth = 0;
    std::size_t bearingStageQueueCapacity = 0;
    std::size_t signalParameterStageQueueDepth = 0;
    std::size_t signalParameterStageQueueCapacity = 0;
    std::uint64_t inFlightBlocks = 0;
};

struct PipelineMetricsSnapshot
{
    std::uint64_t inputBlocks = 0;
    std::uint64_t inputSamples = 0;
    std::uint64_t processedBlocks = 0;
    std::uint64_t processedSamples = 0;
    std::uint64_t droppedBlocks = 0;
    std::uint64_t droppedSamples = 0;
    double inputMegabytesPerSecond = 0.0;
    double processedMegabytesPerSecond = 0.0;
    double rxLatencyMaxMs = 0.0;
    double processingLatencyMaxMs = 0.0;
    double storageLatencyMaxMs = 0.0;
    double guiSnapshotFps = 0.0;
    double maxBlockAgeMs = 0.0;
    LatencyStats processBlockLatency;
    LatencyStats waterfallAggregationLatency;
    LatencyStats spectrumAggregationLatency;
    LatencyStats spectrumSampleLoopLatency;
    LatencyStats spectrumWindowCalculationLatency;
    LatencyStats spectrumBinCalculationLatency;
    LatencyStats spectrumBinUpdateLatency;
    LatencyStats spectrumBandSummaryUpdateLatency;
    LatencyStats spectrumCloseWindowLatency;
    LatencyStats spectrumSnapshotBuildLatency;
    LatencyStats bearingAggregationLatency;
    LatencyStats bearingSampleLoopLatency;
    LatencyStats bearingWindowCalculationLatency;
    LatencyStats bearingBinCalculationLatency;
    LatencyStats bearingCandidateUpdateLatency;
    LatencyStats bearingCloseWindowLatency;
    LatencyStats bearingSnapshotBuildLatency;
    LatencyStats bearingEstimateCalculationLatency;
    LatencyStats signalParameterAggregationLatency;
    LatencyStats signalParameterIngestLatency;
    LatencyStats signalParameterSnapshotDecisionLatency;
    LatencyStats signalParameterFinalizeLatency;
    LatencyStats signalParameterSnapshotBuildLatency;
    LatencyStats parallelFanOutEndToEndLatency;
    LatencyStats waterfallRowPublishLatency;
    LatencyStats spectrumSnapshotPublishLatency;
    LatencyStats bearingSnapshotPublishLatency;
    LatencyStats signalParameterSnapshotPublishLatency;
    std::uint64_t processedBlockSamplesTotal = 0;
    double averageSamplesPerProcessedBlock = 0.0;
    std::uint64_t producedWaterfallRows = 0;
    std::uint64_t producedWaterfallSnapshots = 0;
    std::uint64_t waterfallQueuedRows = 0;
    std::uint64_t waterfallDrainedRows = 0;
    std::uint64_t waterfallDroppedRows = 0;
    std::size_t waterfallRowQueueDepth = 0;
    std::size_t waterfallRowQueueCapacity = 0;
    std::uint64_t latestWaterfallRowSequenceId = 0;
    double waterfallRowUtcDeltaMinMs = 0.0;
    double waterfallRowUtcDeltaMaxMs = 0.0;
    double waterfallExpectedRowPeriodMs = 0.0;
    std::uint64_t waterfallTimebaseMismatchWarnings = 0;
    double aggregationLatencyMaxMs = 0.0;
    std::uint64_t waterfallInvalidFrequencySamples = 0;
    std::uint64_t waterfallOutOfRangeSamples = 0;
    std::uint64_t waterfallEmptyBlocks = 0;
    std::uint64_t producedSpectrumSnapshots = 0;
    std::uint64_t latestSpectrumSnapshotSequence = 0;
    std::uint64_t spectrumInvalidSamples = 0;
    std::uint64_t spectrumOutOfRangeSamples = 0;
    double spectrumAggregationLatencyMaxMs = 0.0;
    std::uint64_t spectrumFastWindowBlocks = 0;
    std::uint64_t spectrumFastBinBlocks = 0;
    std::uint64_t spectrumFastBandSummaryBlocks = 0;
    std::uint64_t spectrumIncrementalWindowBlocks = 0;
    std::uint64_t spectrumIncrementalWindowFallbacks = 0;
    std::uint64_t spectrumBlockLocalAccumulationBlocks = 0;
    std::uint64_t producedBearingSnapshots = 0;
    std::uint64_t producedBearingEstimates = 0;
    std::uint64_t completeBearingCandidates = 0;
    std::uint64_t incompleteBearingCandidates = 0;
    std::uint64_t missingBeam0Candidates = 0;
    std::uint64_t missingBeam1Candidates = 0;
    double bearingAggregationLatencyMaxMs = 0.0;
    std::uint64_t bearingFastCandidateStorageBlocks = 0;
    std::uint64_t bearingBlockLocalAccumulationBlocks = 0;
    std::uint64_t producedSignalParameterSnapshots = 0;
    std::uint64_t signalParameterTrustedFixedBandFastPathBlocks = 0;
    std::uint64_t parallelFanOutBlocks = 0;
    std::uint64_t parallelFanOutFallbackBlocks = 0;
    std::uint64_t parallelFanOutRejectedBlocks = 0;
    StageMetricsSnapshot waterfallStage;
    StageMetricsSnapshot spectrumStage;
    StageMetricsSnapshot bearingStage;
    StageMetricsSnapshot signalParameterStage;
    std::uint64_t waterfallStageDroppedByPolicy = 0;
    std::uint64_t spectrumStageDroppedByPolicy = 0;
    std::uint64_t bearingStageDroppedByPolicy = 0;
    std::uint64_t signalParameterStageDroppedByPolicy = 0;
    std::uint64_t waterfallStageCoalescedByPolicy = 0;
    std::uint64_t spectrumStageCoalescedByPolicy = 0;
    std::uint64_t bearingStageCoalescedByPolicy = 0;
    std::uint64_t signalParameterStageCoalescedByPolicy = 0;
    std::uint64_t waterfallStageSkippedBlocks = 0;
    std::uint64_t spectrumStageSkippedBlocks = 0;
    std::uint64_t bearingStageSkippedBlocks = 0;
    std::uint64_t signalParameterStageSkippedBlocks = 0;
    std::uint64_t visualStageDroppedBlocks = 0;
    std::uint64_t visualStageCoalescedBlocks = 0;
    std::size_t waterfallStageQueueDepth = 0;
    std::size_t spectrumStageQueueDepth = 0;
    std::size_t bearingStageQueueDepth = 0;
    std::size_t signalParameterStageQueueDepth = 0;
    std::uint64_t parallelFanOutInFlightBlocks = 0;
    std::uint64_t waterfallStageProcessedBlocks = 0;
    std::uint64_t spectrumStageProcessedBlocks = 0;
    std::uint64_t bearingStageProcessedBlocks = 0;
    std::uint64_t signalParameterStageProcessedBlocks = 0;
    std::size_t queueDepth = 0;
    std::size_t queueCapacity = 0;
    std::uint64_t queuePushedBlocks = 0;
    std::uint64_t queuePoppedBlocks = 0;
    std::uint64_t queueDroppedBlocks = 0;
    std::size_t blockPoolCapacity = 0;
    std::size_t blockPoolAvailable = 0;
    std::size_t blockPoolInUse = 0;
    double blockPoolUsage = 0.0;
    std::uint64_t blockPoolAcquired = 0;
    std::uint64_t blockPoolReleased = 0;
    std::uint64_t blockPoolExhausted = 0;
};

class PipelineMetrics
{
public:
    void reset();
    void recordInputBlock(std::size_t sampleCount,
                          std::chrono::steady_clock::time_point producedAt);
    void recordDroppedBlock(std::size_t sampleCount);
    void recordProcessedBlock(std::size_t sampleCount,
                              std::chrono::milliseconds blockAge,
                              std::chrono::steady_clock::duration processingLatency);
    void recordProcessBlockLatency(std::chrono::steady_clock::duration elapsed,
                                   std::size_t sampleCount);
    void recordWaterfallAggregationLatency(std::chrono::steady_clock::duration elapsed);
    void recordSpectrumAggregationLatency(std::chrono::steady_clock::duration elapsed);
    void recordSpectrumSampleLoopLatency(std::chrono::steady_clock::duration elapsed);
    void recordSpectrumWindowCalculationLatency(
        std::chrono::steady_clock::duration elapsed);
    void recordSpectrumBinCalculationLatency(std::chrono::steady_clock::duration elapsed);
    void recordSpectrumBinUpdateLatency(std::chrono::steady_clock::duration elapsed);
    void recordSpectrumBandSummaryUpdateLatency(
        std::chrono::steady_clock::duration elapsed);
    void recordSpectrumCloseWindowLatency(std::chrono::steady_clock::duration elapsed);
    void recordSpectrumSnapshotBuildLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingAggregationLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingSampleLoopLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingWindowCalculationLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingBinCalculationLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingCandidateUpdateLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingCloseWindowLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingSnapshotBuildLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingEstimateCalculationLatency(std::chrono::steady_clock::duration elapsed);
    void recordSignalParameterAggregationLatency(std::chrono::steady_clock::duration elapsed);
    void recordSignalParameterIngestLatency(std::chrono::steady_clock::duration elapsed);
    void recordSignalParameterSnapshotDecisionLatency(
        std::chrono::steady_clock::duration elapsed);
    void recordSignalParameterFinalizeLatency(std::chrono::steady_clock::duration elapsed);
    void recordSignalParameterSnapshotBuildLatency(
        std::chrono::steady_clock::duration elapsed);
    void recordWaterfallRowPublishLatency(std::chrono::steady_clock::duration elapsed);
    void recordSpectrumSnapshotPublishLatency(std::chrono::steady_clock::duration elapsed);
    void recordBearingSnapshotPublishLatency(std::chrono::steady_clock::duration elapsed);
    void recordSignalParameterSnapshotPublishLatency(
        std::chrono::steady_clock::duration elapsed);
    void recordWaterfallAggregation(std::uint64_t producedRows,
                                    std::uint64_t producedSnapshots,
                                    std::uint64_t invalidFrequencySamples,
                                    std::uint64_t outOfRangeSamples,
                                    std::uint64_t emptyBlocks,
                                    std::chrono::milliseconds aggregationLatency);
    void recordSpectrumAggregation(std::uint64_t producedSnapshots,
                                   std::uint64_t latestSnapshotSequence,
                                   std::uint64_t invalidSamples,
                                   std::uint64_t outOfRangeSamples,
                                   std::chrono::milliseconds aggregationLatency);
    void recordBearingAggregation(std::uint64_t producedSnapshots,
                                  std::uint64_t producedEstimates,
                                  std::uint64_t completeCandidates,
                                  std::uint64_t incompleteCandidates,
                                  std::uint64_t missingBeam0Candidates,
                                  std::uint64_t missingBeam1Candidates,
                                  std::chrono::milliseconds aggregationLatency);
    void recordSignalParameterSnapshotProduced(std::uint64_t producedSnapshots);
    void recordSignalParameterTrustedFixedBandFastPathBlock();
    void recordParallelFanOutBlock();
    void recordParallelFanOutFallbackBlock();
    void recordParallelFanOutRejectedBlock();
    void recordStageEnqueued(PipelineStageMetric stage,
                             std::size_t queueDepth,
                             std::size_t capacity);
    void recordStageSubmitFailure(PipelineStageMetric stage,
                                  std::size_t queueDepth,
                                  std::size_t capacity);
    void recordStageStarted(PipelineStageMetric stage,
                            std::chrono::steady_clock::duration queueWait,
                            std::size_t queueDepth,
                            std::size_t capacity);
    void recordStageCompleted(PipelineStageMetric stage,
                              std::chrono::steady_clock::duration serviceLatency,
                              std::size_t sampleCount);
    void recordStageQueueDepth(PipelineStageMetric stage,
                               std::size_t queueDepth,
                               std::size_t capacity);
    void recordStageDroppedByPolicy(PipelineStageMetric stage, std::size_t count);
    void recordStageCoalescedByPolicy(PipelineStageMetric stage, std::size_t count);
    void recordStageSkipped(PipelineStageMetric stage, StageSkipReason reason);
    void recordWaterfallStageProcessedBlock();
    void recordSpectrumStageProcessedBlock();
    void recordBearingStageProcessedBlock();
    void recordSignalParameterStageProcessedBlock();
    void recordParallelFanOutEndToEndLatency(
        std::chrono::steady_clock::duration elapsed);
    void recordBearingFastCandidateStorageBlock();
    void recordBearingBlockLocalAccumulationBlock();
    void recordSpectrumFastPathUsage(bool fastWindow,
                                     bool fastBin,
                                     bool fastBandSummary);
    void recordSpectrumIncrementalWindowUsage(bool incrementalWindow,
                                             std::uint64_t fallbackCount);
    void recordSpectrumBlockLocalAccumulationBlock();

    PipelineMetricsSnapshot snapshot(
        const BoundedBlockQueueMetrics& queueMetrics,
        const SignalBlockPoolCounters& poolCounters,
        const WaterfallRowQueueMetrics& waterfallRowMetrics = {},
        const ParallelFanOutQueueMetrics& parallelFanOutQueueMetrics = {}) const;

private:
    static double megabytesForSamples(std::uint64_t sampleCount);
    static double millisecondsFor(std::chrono::steady_clock::duration elapsed);
    static void recordLatency(LatencyStats& stats, double elapsedMs);
    static std::size_t stageMetricIndex(PipelineStageMetric stage) noexcept;

    mutable std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_startedAt = std::chrono::steady_clock::now();
    std::uint64_t m_inputBlocks = 0;
    std::uint64_t m_inputSamples = 0;
    std::uint64_t m_processedBlocks = 0;
    std::uint64_t m_processedSamples = 0;
    std::uint64_t m_droppedBlocks = 0;
    std::uint64_t m_droppedSamples = 0;
    std::uint64_t m_producedWaterfallRows = 0;
    std::uint64_t m_producedWaterfallSnapshots = 0;
    std::uint64_t m_waterfallInvalidFrequencySamples = 0;
    std::uint64_t m_waterfallOutOfRangeSamples = 0;
    std::uint64_t m_waterfallEmptyBlocks = 0;
    std::uint64_t m_producedSpectrumSnapshots = 0;
    std::uint64_t m_latestSpectrumSnapshotSequence = 0;
    std::uint64_t m_spectrumInvalidSamples = 0;
    std::uint64_t m_spectrumOutOfRangeSamples = 0;
    std::uint64_t m_producedBearingSnapshots = 0;
    std::uint64_t m_producedBearingEstimates = 0;
    std::uint64_t m_completeBearingCandidates = 0;
    std::uint64_t m_incompleteBearingCandidates = 0;
    std::uint64_t m_missingBeam0Candidates = 0;
    std::uint64_t m_missingBeam1Candidates = 0;
    std::uint64_t m_producedSignalParameterSnapshots = 0;
    std::uint64_t m_signalParameterTrustedFixedBandFastPathBlocks = 0;
    std::chrono::milliseconds m_rxLatencyMax{0};
    std::chrono::milliseconds m_processingLatencyMax{0};
    std::chrono::milliseconds m_aggregationLatencyMax{0};
    std::chrono::milliseconds m_spectrumAggregationLatencyMax{0};
    std::chrono::milliseconds m_bearingAggregationLatencyMax{0};
    std::chrono::milliseconds m_maxBlockAge{0};
    LatencyStats m_processBlockLatency;
    LatencyStats m_waterfallAggregationLatency;
    LatencyStats m_spectrumAggregationLatency;
    LatencyStats m_spectrumSampleLoopLatency;
    LatencyStats m_spectrumWindowCalculationLatency;
    LatencyStats m_spectrumBinCalculationLatency;
    LatencyStats m_spectrumBinUpdateLatency;
    LatencyStats m_spectrumBandSummaryUpdateLatency;
    LatencyStats m_spectrumCloseWindowLatency;
    LatencyStats m_spectrumSnapshotBuildLatency;
    LatencyStats m_bearingAggregationLatency;
    LatencyStats m_bearingSampleLoopLatency;
    LatencyStats m_bearingWindowCalculationLatency;
    LatencyStats m_bearingBinCalculationLatency;
    LatencyStats m_bearingCandidateUpdateLatency;
    LatencyStats m_bearingCloseWindowLatency;
    LatencyStats m_bearingSnapshotBuildLatency;
    LatencyStats m_bearingEstimateCalculationLatency;
    LatencyStats m_signalParameterAggregationLatency;
    LatencyStats m_signalParameterIngestLatency;
    LatencyStats m_signalParameterSnapshotDecisionLatency;
    LatencyStats m_signalParameterFinalizeLatency;
    LatencyStats m_signalParameterSnapshotBuildLatency;
    LatencyStats m_parallelFanOutEndToEndLatency;
    LatencyStats m_waterfallRowPublishLatency;
    LatencyStats m_spectrumSnapshotPublishLatency;
    LatencyStats m_bearingSnapshotPublishLatency;
    LatencyStats m_signalParameterSnapshotPublishLatency;
    std::uint64_t m_processedBlockSamplesTotal = 0;
    std::uint64_t m_parallelFanOutBlocks = 0;
    std::uint64_t m_parallelFanOutFallbackBlocks = 0;
    std::uint64_t m_parallelFanOutRejectedBlocks = 0;
    std::array<StageMetricsSnapshot, 4> m_stageMetrics;
    std::uint64_t m_bearingFastCandidateStorageBlocks = 0;
    std::uint64_t m_bearingBlockLocalAccumulationBlocks = 0;
    std::uint64_t m_spectrumFastWindowBlocks = 0;
    std::uint64_t m_spectrumFastBinBlocks = 0;
    std::uint64_t m_spectrumFastBandSummaryBlocks = 0;
    std::uint64_t m_spectrumIncrementalWindowBlocks = 0;
    std::uint64_t m_spectrumIncrementalWindowFallbacks = 0;
    std::uint64_t m_spectrumBlockLocalAccumulationBlocks = 0;
};

} // namespace siriusscope::pipeline
