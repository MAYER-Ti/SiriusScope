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
    m_rxLatencyMax = std::chrono::milliseconds{0};
    m_processingLatencyMax = std::chrono::milliseconds{0};
    m_maxBlockAge = std::chrono::milliseconds{0};
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
                                           std::chrono::milliseconds processingLatency)
{
    std::lock_guard lock(m_mutex);
    ++m_processedBlocks;
    m_processedSamples += static_cast<std::uint64_t>(sampleCount);
    m_maxBlockAge = std::max(m_maxBlockAge, blockAge);
    m_processingLatencyMax = std::max(m_processingLatencyMax, processingLatency);
}

PipelineMetricsSnapshot PipelineMetrics::snapshot(
    const BoundedBlockQueueMetrics& queueMetrics,
    const SignalBlockPoolCounters& poolCounters) const
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

} // namespace siriusscope::pipeline
