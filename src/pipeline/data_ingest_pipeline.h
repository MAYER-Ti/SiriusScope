#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "pipeline/bearing_aggregator.h"
#include "pipeline/bearing_snapshot.h"
#include "pipeline/bounded_block_queue.h"
#include "pipeline/pipeline_diagnostics.h"
#include "pipeline/pipeline_metrics.h"
#include "pipeline/processing_engine.h"
#include "pipeline/signal_block_pool.h"
#include "pipeline/snapshot_exchange.h"
#include "pipeline/spectrum_aggregator.h"
#include "pipeline/spectrum_snapshot.h"
#include "pipeline/waterfall_row_queue.h"
#include "pipeline/waterfall_snapshot.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

struct DataIngestPipelineConfig
{
    SignalBlockPoolConfig blockPool;
    std::size_t queueCapacity = 64;
    std::chrono::milliseconds diagnosticsPublishInterval{1000};
    bool acceptingOnStart = false;
    WaterfallAggregatorConfig waterfall;
    WaterfallRowQueueConfig waterfallRows;
    SpectrumAggregatorConfig spectrum;
    BearingAggregatorConfig bearing;
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
    std::vector<WaterfallQueuedRow> drainWaterfallRows(std::size_t maxRows);
    WaterfallRowQueueMetrics waterfallRowQueueMetrics() const;
    std::shared_ptr<const SpectrumSnapshot> latestSpectrumSnapshot() const;
    std::shared_ptr<const BearingSnapshot> latestBearingSnapshot() const;
    void configureWaterfall(WaterfallAggregatorConfig config);
    void configureSpectrum(SpectrumAggregatorConfig config);
    void configureBearing(BearingAggregatorConfig config);
    void clearQueuedBlocks();

private:
    void recordDroppedBlock(std::size_t sampleCount);

    DataIngestPipelineConfig m_config;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    SignalBlockPool m_pool;
    BoundedBlockQueue m_queue;
    PipelineMetrics m_metrics;
    PipelineDiagnostics m_diagnostics;
    WaterfallRowQueue m_waterfallRows;
    SnapshotExchange<SpectrumSnapshot> m_spectrumSnapshots;
    SnapshotExchange<BearingSnapshot> m_bearingSnapshots;
    ProcessingEngine m_engine;
    std::atomic_bool m_accepting{false};
    std::atomic_bool m_running{false};
    std::atomic<std::uint64_t> m_nextSequenceId{1};
};

} // namespace siriusscope::pipeline
