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

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
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

enum class ProcessingMode
{
    Sequential,
    ParallelFanOut,
};

struct ProcessingEngineConfig
{
    ProcessingMode processingMode = ProcessingMode::Sequential;
    std::size_t stageQueueCapacity = 64;
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
                     SignalParameterAggregatorConfig signalParameterConfig = {},
                     ProcessingEngineConfig processingConfig = {});
    ~ProcessingEngine();

    ProcessingEngine(const ProcessingEngine&) = delete;
    ProcessingEngine& operator=(const ProcessingEngine&) = delete;

    core::OperationResult start();
    void stop();
    core::OperationResult flush(std::chrono::milliseconds timeout);
    ProcessingEngineSummary lastSummary() const;
    ParallelFanOutQueueMetrics parallelFanOutQueueMetrics() const;
    bool running() const noexcept;
    void setWaterfallConfig(WaterfallAggregatorConfig config);
    void setSpectrumConfig(SpectrumAggregatorConfig config);
    void setBearingConfig(BearingAggregatorConfig config);
    void setSignalParameterConfig(SignalParameterAggregatorConfig config);
    std::shared_ptr<const SignalParameterSnapshot> forceSignalParameterSnapshot();

private:
    enum class FanOutStage
    {
        Waterfall,
        Spectrum,
        Bearing,
        SignalParameter,
    };

    struct BlockAccountingData
    {
        std::uint64_t sampleCount = 0;
        std::uint64_t firstSampleIndex = 0;
        std::uint64_t lastSampleIndex = 0;
        bool hasSamples = false;
        std::chrono::milliseconds blockAge{0};
        std::vector<BandBeamAggregateSummary> bandBeamSummaries;
    };

    struct FanOutBlockContext
    {
        SignalBlockHandle block;
        BlockAccountingData accounting;
        std::chrono::steady_clock::time_point startedAt{};
        std::atomic<int> remainingStages{0};
    };

    struct StageJob
    {
        std::shared_ptr<FanOutBlockContext> context;
        std::chrono::steady_clock::time_point enqueuedAt{};
        std::size_t queueDepthAfterPop = 0;
        std::size_t queueCapacity = 0;
    };

    static constexpr std::size_t kFanOutStageCount = 4;

    void workerLoop();
    void processBlock(SignalBlockHandle block);
    void processBlockSequential(const SignalBlock& block);
    void processBlockParallel(SignalBlockHandle block);
    BlockAccountingData makeBlockAccounting(
        const SignalBlock& block,
        std::chrono::steady_clock::time_point startedAt) const;
    void recordBlockCompleted(const BlockAccountingData& accounting,
                              std::chrono::steady_clock::duration processingElapsed);
    void processWaterfallStage(const SignalBlock& block);
    void processSpectrumStage(const SignalBlock& block);
    void processBearingStage(const SignalBlock& block);
    void processSignalParameterStage(const SignalBlock& block);
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
    void startFanOutWorkers();
    void stopFanOutWorkers();
    void resetFanOutQueues();
    bool submitFanOutContext(const std::shared_ptr<FanOutBlockContext>& context);
    StageJob popFanOutStageJob(FanOutStage stage);
    void stageWorkerLoop(FanOutStage stage);
    void processFanOutStage(FanOutStage stage, const SignalBlock& block);
    void completeFanOutStage(const std::shared_ptr<FanOutBlockContext>& context);
    bool fanOutDrained() const;
    bool stageWorkersJoinable() const noexcept;
    static std::size_t fanOutStageIndex(FanOutStage stage) noexcept;
    static PipelineStageMetric fanOutStageMetric(FanOutStage stage) noexcept;
    static PipelineStageMetric fanOutStageMetricAt(std::size_t index) noexcept;

    BoundedBlockQueue* m_queue = nullptr;
    PipelineMetrics* m_metrics = nullptr;
    PipelineDiagnostics* m_diagnostics = nullptr;
    WaterfallRowQueue* m_waterfallRows = nullptr;
    SnapshotExchange<SpectrumSnapshot>* m_spectrumSnapshots = nullptr;
    SnapshotExchange<BearingSnapshot>* m_bearingSnapshots = nullptr;
    SnapshotExchange<SignalParameterSnapshot>* m_signalParameterSnapshots = nullptr;
    ProcessingEngineConfig m_processingConfig;

    mutable std::mutex m_mutex;
    mutable std::mutex m_waterfallMutex;
    mutable std::mutex m_spectrumMutex;
    mutable std::mutex m_bearingMutex;
    mutable std::mutex m_signalParameterMutex;
    mutable std::mutex m_stageMutex;
    std::condition_variable m_flushCondition;
    std::condition_variable m_stageCondition;
    std::thread m_worker;
    std::array<std::thread, kFanOutStageCount> m_stageWorkers;
    std::array<std::deque<StageJob>, kFanOutStageCount> m_stageQueues;
    ProcessingEngineSummary m_summary;
    WaterfallAggregator m_waterfallAggregator;
    SpectrumAggregator m_spectrumAggregator;
    BearingAggregator m_bearingAggregator;
    SignalParameterAggregator m_signalParameterAggregator;
    std::uint64_t m_nextWaterfallSourceSnapshotSequenceId = 1;
    std::uint64_t m_completedQueuePops = 0;
    std::atomic<std::uint64_t> m_inFlightFanOutBlocks{0};
    bool m_running = false;
    bool m_processingBlock = false;
    bool m_stageStopRequested = false;
};

} // namespace siriusscope::pipeline
