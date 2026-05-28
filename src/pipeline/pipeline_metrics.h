#pragma once

#include "pipeline/bounded_block_queue.h"
#include "pipeline/signal_block_pool.h"

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

    PipelineMetricsSnapshot snapshot(const BoundedBlockQueueMetrics& queueMetrics,
                                     const SignalBlockPoolCounters& poolCounters) const;

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
    std::chrono::milliseconds m_rxLatencyMax{0};
    std::chrono::milliseconds m_processingLatencyMax{0};
    std::chrono::milliseconds m_maxBlockAge{0};
};

} // namespace siriusscope::pipeline
