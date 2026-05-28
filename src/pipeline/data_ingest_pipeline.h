#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "pipeline/bounded_block_queue.h"
#include "pipeline/pipeline_diagnostics.h"
#include "pipeline/pipeline_metrics.h"
#include "pipeline/processing_engine.h"
#include "pipeline/signal_block_pool.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>

namespace siriusscope::pipeline {

struct DataIngestPipelineConfig
{
    SignalBlockPoolConfig blockPool;
    std::size_t queueCapacity = 64;
    std::chrono::milliseconds diagnosticsPublishInterval{1000};
    bool acceptingOnStart = false;
};

class DataIngestPipeline
{
public:
    explicit DataIngestPipeline(
        DataIngestPipelineConfig config = {},
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr);
    ~DataIngestPipeline();

    DataIngestPipeline(const DataIngestPipeline&) = delete;
    DataIngestPipeline& operator=(const DataIngestPipeline&) = delete;

    core::OperationResult start();
    void stop();
    void setAccepting(bool accepting) noexcept;
    bool accepting() const noexcept;
    bool running() const noexcept;

    core::OperationResult ingestSamples(std::span<const core::SignalSample> samples,
                                        SignalBlockMetadata metadata = {});
    core::OperationResult flushProcessing(std::chrono::milliseconds timeout);
    PipelineMetricsSnapshot metricsSnapshot() const;
    ProcessingEngineSummary lastSummary() const;
    void clearQueuedBlocks();

private:
    void recordDroppedBlock(std::size_t sampleCount);

    DataIngestPipelineConfig m_config;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    SignalBlockPool m_pool;
    BoundedBlockQueue m_queue;
    PipelineMetrics m_metrics;
    PipelineDiagnostics m_diagnostics;
    ProcessingEngine m_engine;
    std::atomic_bool m_accepting{false};
    std::atomic_bool m_running{false};
    std::atomic<std::uint64_t> m_nextSequenceId{1};
};

} // namespace siriusscope::pipeline
