#pragma once

#include "core/operation_result.h"
#include "pipeline/bounded_block_queue.h"
#include "pipeline/pipeline_diagnostics.h"
#include "pipeline/pipeline_metrics.h"
#include "pipeline/snapshot_exchange.h"
#include "pipeline/spectrum_aggregator.h"
#include "pipeline/spectrum_snapshot.h"
#include "pipeline/waterfall_aggregator.h"
#include "pipeline/waterfall_snapshot.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace siriusscope::pipeline {

struct BandBeamAggregateSummary
{
    int bandIndex = 0;
    int beamIndex = 0;
    std::uint64_t sampleCount = 0;
    int maxAmplitude = 0;
};

struct ProcessingEngineSummary
{
    std::uint64_t processedBlocks = 0;
    std::uint64_t processedSamples = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
    std::vector<BandBeamAggregateSummary> bandBeamSummaries;
    PipelineMetricsSnapshot metrics;
};

class ProcessingEngine
{
public:
    ProcessingEngine(BoundedBlockQueue* queue,
                     PipelineMetrics* metrics,
                     PipelineDiagnostics* diagnostics = nullptr,
                     SnapshotExchange<WaterfallSnapshot>* waterfallSnapshots = nullptr,
                     WaterfallAggregatorConfig waterfallConfig = {},
                     SnapshotExchange<SpectrumSnapshot>* spectrumSnapshots = nullptr,
                     SpectrumAggregatorConfig spectrumConfig = {});
    ~ProcessingEngine();

    ProcessingEngine(const ProcessingEngine&) = delete;
    ProcessingEngine& operator=(const ProcessingEngine&) = delete;

    core::OperationResult start();
    void stop();
    core::OperationResult flush(std::chrono::milliseconds timeout);
    ProcessingEngineSummary lastSummary() const;
    bool running() const noexcept;
    void setWaterfallConfig(WaterfallAggregatorConfig config);
    void setSpectrumConfig(SpectrumAggregatorConfig config);

private:
    void workerLoop();
    void processBlock(const SignalBlock& block);
    void publishWaterfallRows(WaterfallAggregationResult result,
                              std::chrono::milliseconds aggregationLatency);
    void publishSpectrumSnapshots(SpectrumAggregationResult result,
                                  std::chrono::milliseconds aggregationLatency);
    void flushWaterfallRows();
    void flushSpectrumSnapshots();

    BoundedBlockQueue* m_queue = nullptr;
    PipelineMetrics* m_metrics = nullptr;
    PipelineDiagnostics* m_diagnostics = nullptr;
    SnapshotExchange<WaterfallSnapshot>* m_waterfallSnapshots = nullptr;
    SnapshotExchange<SpectrumSnapshot>* m_spectrumSnapshots = nullptr;

    mutable std::mutex m_mutex;
    mutable std::mutex m_waterfallMutex;
    mutable std::mutex m_spectrumMutex;
    std::condition_variable m_flushCondition;
    std::thread m_worker;
    ProcessingEngineSummary m_summary;
    WaterfallAggregator m_waterfallAggregator;
    SpectrumAggregator m_spectrumAggregator;
    bool m_running = false;
    bool m_processingBlock = false;
};

} // namespace siriusscope::pipeline
