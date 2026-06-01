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
    m_rxLatencyMax = std::chrono::milliseconds{0};
    m_processingLatencyMax = std::chrono::milliseconds{0};
    m_aggregationLatencyMax = std::chrono::milliseconds{0};
    m_spectrumAggregationLatencyMax = std::chrono::milliseconds{0};
    m_bearingAggregationLatencyMax = std::chrono::milliseconds{0};
    m_maxBlockAge = std::chrono::milliseconds{0};
    m_processBlockLatency = {};
    m_waterfallAggregationLatency = {};
    m_spectrumAggregationLatency = {};
    m_bearingAggregationLatency = {};
    m_signalParameterAggregationLatency = {};
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

void PipelineMetrics::recordBearingAggregationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_bearingAggregationLatency, millisecondsFor(elapsed));
}

void PipelineMetrics::recordSignalParameterAggregationLatency(
    std::chrono::steady_clock::duration elapsed)
{
    std::lock_guard lock(m_mutex);
    recordLatency(m_signalParameterAggregationLatency, millisecondsFor(elapsed));
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

PipelineMetricsSnapshot PipelineMetrics::snapshot(
    const BoundedBlockQueueMetrics& queueMetrics,
    const SignalBlockPoolCounters& poolCounters,
    const WaterfallRowQueueMetrics& waterfallRowMetrics) const
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
    snapshot.bearingAggregationLatency = m_bearingAggregationLatency;
    snapshot.signalParameterAggregationLatency = m_signalParameterAggregationLatency;
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
    snapshot.producedBearingSnapshots = m_producedBearingSnapshots;
    snapshot.producedBearingEstimates = m_producedBearingEstimates;
    snapshot.completeBearingCandidates = m_completeBearingCandidates;
    snapshot.incompleteBearingCandidates = m_incompleteBearingCandidates;
    snapshot.missingBeam0Candidates = m_missingBeam0Candidates;
    snapshot.missingBeam1Candidates = m_missingBeam1Candidates;
    snapshot.bearingAggregationLatencyMaxMs =
        static_cast<double>(m_bearingAggregationLatencyMax.count());
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

} // namespace siriusscope::pipeline
