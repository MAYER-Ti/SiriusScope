#pragma once

#include "core/operation_result.h"
#include "pipeline/bearing_aggregator.h"
#include "pipeline/bearing_snapshot.h"
#include "pipeline/bounded_block_queue.h"
#include "pipeline/pipeline_diagnostics.h"
#include "pipeline/pipeline_metrics.h"
#include "pipeline/snapshot_exchange.h"
#include "pipeline/signal_parameter_aggregator.h"
#include "pipeline/signal_parameter_snapshot.h"
#include "pipeline/spectrum_aggregator.h"
#include "pipeline/spectrum_snapshot.h"
#include "pipeline/waterfall_aggregator.h"
#include "pipeline/waterfall_row_queue.h"
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
                     WaterfallRowQueue* waterfallRows = nullptr,
                     WaterfallAggregatorConfig waterfallConfig = {},
                     SnapshotExchange<SpectrumSnapshot>* spectrumSnapshots = nullptr,
                     SpectrumAggregatorConfig spectrumConfig = {},
                     SnapshotExchange<BearingSnapshot>* bearingSnapshots = nullptr,
                     BearingAggregatorConfig bearingConfig = {},
                     SnapshotExchange<SignalParameterSnapshot>* signalParameterSnapshots = nullptr,
                     SignalParameterAggregatorConfig signalParameterConfig = {});
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
    void setBearingConfig(BearingAggregatorConfig config);
    void setSignalParameterConfig(SignalParameterAggregatorConfig config);

private:
    void workerLoop();
    void processBlock(const SignalBlock& block);
    void publishWaterfallRows(WaterfallAggregationResult result,
                              WaterfallAggregatorConfig config,
                              std::chrono::milliseconds aggregationLatency,
                              std::chrono::steady_clock::duration* publishLatency = nullptr);
    void publishSpectrumSnapshots(SpectrumAggregationResult result,
                                  std::chrono::milliseconds aggregationLatency,
                                  std::chrono::steady_clock::duration* publishLatency = nullptr);
    void publishBearingSnapshots(BearingAggregationResult result,
                                 std::chrono::milliseconds aggregationLatency,
                                 std::chrono::steady_clock::duration* publishLatency = nullptr);
    void publishSignalParameterSnapshots(SignalParameterAggregationResult result,
                                         std::chrono::milliseconds aggregationLatency,
                                         std::chrono::steady_clock::duration* publishLatency = nullptr);
    void flushWaterfallRows();
    void flushSpectrumSnapshots();
    void flushBearingSnapshots();

    BoundedBlockQueue* m_queue = nullptr;
    PipelineMetrics* m_metrics = nullptr;
    PipelineDiagnostics* m_diagnostics = nullptr;
    WaterfallRowQueue* m_waterfallRows = nullptr;
    SnapshotExchange<SpectrumSnapshot>* m_spectrumSnapshots = nullptr;
    SnapshotExchange<BearingSnapshot>* m_bearingSnapshots = nullptr;
    SnapshotExchange<SignalParameterSnapshot>* m_signalParameterSnapshots = nullptr;

    mutable std::mutex m_mutex;
    mutable std::mutex m_waterfallMutex;
    mutable std::mutex m_spectrumMutex;
    mutable std::mutex m_bearingMutex;
    mutable std::mutex m_signalParameterMutex;
    std::condition_variable m_flushCondition;
    std::thread m_worker;
    ProcessingEngineSummary m_summary;
    WaterfallAggregator m_waterfallAggregator;
    SpectrumAggregator m_spectrumAggregator;
    BearingAggregator m_bearingAggregator;
    SignalParameterAggregator m_signalParameterAggregator;
    std::uint64_t m_nextWaterfallSourceSnapshotSequenceId = 1;
    std::uint64_t m_completedQueuePops = 0;
    bool m_running = false;
    bool m_processingBlock = false;
};

} // namespace siriusscope::pipeline
