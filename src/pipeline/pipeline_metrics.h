#pragma once

#include "pipeline/bounded_block_queue.h"
#include "pipeline/signal_block_pool.h"
#include "pipeline/waterfall_row_queue.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace siriusscope::pipeline {

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
    std::uint64_t producedBearingSnapshots = 0;
    std::uint64_t producedBearingEstimates = 0;
    std::uint64_t completeBearingCandidates = 0;
    std::uint64_t incompleteBearingCandidates = 0;
    std::uint64_t missingBeam0Candidates = 0;
    std::uint64_t missingBeam1Candidates = 0;
    double bearingAggregationLatencyMaxMs = 0.0;
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
                              std::chrono::milliseconds processingLatency);
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

    PipelineMetricsSnapshot snapshot(
        const BoundedBlockQueueMetrics& queueMetrics,
        const SignalBlockPoolCounters& poolCounters,
        const WaterfallRowQueueMetrics& waterfallRowMetrics = {}) const;

private:
    static double megabytesForSamples(std::uint64_t sampleCount);

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
    std::chrono::milliseconds m_rxLatencyMax{0};
    std::chrono::milliseconds m_processingLatencyMax{0};
    std::chrono::milliseconds m_aggregationLatencyMax{0};
    std::chrono::milliseconds m_spectrumAggregationLatencyMax{0};
    std::chrono::milliseconds m_bearingAggregationLatencyMax{0};
    std::chrono::milliseconds m_maxBlockAge{0};
};

} // namespace siriusscope::pipeline
