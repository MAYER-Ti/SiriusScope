#pragma once

#include "core/operation_result.h"
#include "hardware/interfaces/bco_stream_source.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace siriusscope::pipeline {

class DataIngestPipeline;

enum class RxOverflowPolicy
{
    DropNewest,
    DropOldest,
};

struct SourceToPipelineBridgeConfig
{
    std::size_t queueCapacity = 8;
    RxOverflowPolicy overflowPolicy = RxOverflowPolicy::DropNewest;
};

struct SourceToPipelineBridgeMetrics
{
    std::uint64_t receivedBlocks = 0;
    std::uint64_t receivedSamples = 0;

    std::uint64_t enqueuedBlocks = 0;
    std::uint64_t enqueuedSamples = 0;

    std::uint64_t droppedBlocks = 0;
    std::uint64_t droppedSamples = 0;

    std::uint64_t ingestedBlocks = 0;
    std::uint64_t ingestedSamples = 0;

    std::uint64_t rejectedBlocks = 0;
    std::uint64_t rejectedSamples = 0;

    std::size_t queueDepth = 0;
    std::size_t queueCapacity = 0;

    std::chrono::milliseconds enqueueLatencyMax{0};
    std::chrono::milliseconds ingestLatencyMax{0};
};

class SourceToPipelineBridge
{
public:
    explicit SourceToPipelineBridge(
        DataIngestPipeline* pipeline,
        SourceToPipelineBridgeConfig config = {},
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr);
    ~SourceToPipelineBridge();

    SourceToPipelineBridge(const SourceToPipelineBridge&) = delete;
    SourceToPipelineBridge& operator=(const SourceToPipelineBridge&) = delete;

    core::OperationResult start();
    void stop();

    bool running() const noexcept;

    // This method is intended to be called directly from IBcoStreamSource callback.
    void submit(hardware::IBcoStreamSource::SampleBlockPtr block);

    core::OperationResult flush(std::chrono::milliseconds timeout);
    SourceToPipelineBridgeMetrics metrics() const;

    void clear();

private:
    void workerLoop();
    void recordDropLocked(const hardware::BcoSampleBlock& block);
    void recordEnqueueLatencyLocked(std::chrono::steady_clock::time_point startedAt);
    void publish(infrastructure::DiagnosticSeverity severity,
                 const std::string& message) const;

    DataIngestPipeline* m_pipeline = nullptr;
    SourceToPipelineBridgeConfig m_config;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<hardware::IBcoStreamSource::SampleBlockPtr> m_queue;
    std::thread m_worker;

    SourceToPipelineBridgeMetrics m_metrics;
    std::atomic_bool m_running{false};
    bool m_stopRequested = false;
    bool m_ingestInProgress = false;
    bool m_overflowDiagnosticPublished = false;
};

} // namespace siriusscope::pipeline
