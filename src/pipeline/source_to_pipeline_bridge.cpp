#include "pipeline/source_to_pipeline_bridge.h"

#include "pipeline/data_ingest_pipeline.h"
#include "pipeline/signal_block.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace siriusscope::pipeline {

namespace {

constexpr const char* kSubsystem = "SourceToPipelineBridge";

std::chrono::milliseconds elapsedMilliseconds(
    std::chrono::steady_clock::time_point startedAt)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt);
}

std::uint64_t sampleCountOf(const hardware::IBcoStreamSource::SampleBlockPtr& block)
{
    return block ? static_cast<std::uint64_t>(block->samples.size()) : 0;
}

} // namespace

SourceToPipelineBridge::SourceToPipelineBridge(
    DataIngestPipeline* pipeline,
    SourceToPipelineBridgeConfig config,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_pipeline(pipeline)
    , m_config(config)
    , m_diagnosticsSink(diagnosticsSink)
{
    m_metrics.queueCapacity = m_config.queueCapacity;
}

SourceToPipelineBridge::~SourceToPipelineBridge()
{
    stop();
}

core::OperationResult SourceToPipelineBridge::start()
{
    if (!m_pipeline) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "source-to-pipeline bridge cannot start without data ingest pipeline");
        return core::OperationResult::failure(
            "source-to-pipeline bridge pipeline is not configured");
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_running.load(std::memory_order_acquire)) {
            return core::OperationResult::ok();
        }

        m_queue.clear();
        m_metrics = {};
        m_metrics.queueCapacity = m_config.queueCapacity;
        m_stopRequested = false;
        m_ingestInProgress = false;
        m_overflowDiagnosticPublished = false;
        m_running.store(true, std::memory_order_release);
    }

    try {
        m_worker = std::thread(&SourceToPipelineBridge::workerLoop, this);
    } catch (const std::exception& error) {
        {
            std::lock_guard lock(m_mutex);
            m_running.store(false, std::memory_order_release);
            m_stopRequested = true;
        }
        publish(infrastructure::DiagnosticSeverity::Error,
                std::string("source-to-pipeline bridge worker failed to start: ")
                    + error.what());
        return core::OperationResult::failure(
            "source-to-pipeline bridge worker failed to start");
    }

    return core::OperationResult::ok();
}

void SourceToPipelineBridge::stop()
{
    std::uint64_t discardedBlocks = 0;
    std::uint64_t discardedSamples = 0;

    {
        std::lock_guard lock(m_mutex);
        if (!m_running.load(std::memory_order_acquire) && !m_worker.joinable()) {
            return;
        }

        m_stopRequested = true;
        discardedBlocks = static_cast<std::uint64_t>(m_queue.size());
        for (const auto& block : m_queue) {
            discardedSamples += sampleCountOf(block);
            if (block) {
                recordDropLocked(*block);
            }
        }
        m_queue.clear();
        m_metrics.queueDepth = 0;
    }

    m_condition.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    {
        std::lock_guard lock(m_mutex);
        m_running.store(false, std::memory_order_release);
        m_stopRequested = false;
        m_ingestInProgress = false;
    }
    m_condition.notify_all();

    if (discardedBlocks > 0) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "source-to-pipeline bridge discarded queued RX blocks on stop: blocks="
                    + std::to_string(discardedBlocks) + " samples="
                    + std::to_string(discardedSamples));
    }
}

bool SourceToPipelineBridge::running() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

void SourceToPipelineBridge::submit(hardware::IBcoStreamSource::SampleBlockPtr block)
{
    if (!block) {
        return;
    }

    const auto startedAt = std::chrono::steady_clock::now();
    bool notifyWorker = false;
    bool publishOverflow = false;

    {
        std::lock_guard lock(m_mutex);
        ++m_metrics.receivedBlocks;
        m_metrics.receivedSamples += static_cast<std::uint64_t>(block->samples.size());

        if (!m_running.load(std::memory_order_acquire) || m_stopRequested) {
            recordDropLocked(*block);
            recordEnqueueLatencyLocked(startedAt);
        } else if (m_queue.size() >= m_config.queueCapacity) {
            if (!m_overflowDiagnosticPublished) {
                m_overflowDiagnosticPublished = true;
                publishOverflow = true;
            }

            if (m_config.overflowPolicy == RxOverflowPolicy::DropNewest) {
                recordDropLocked(*block);
                recordEnqueueLatencyLocked(startedAt);
            } else if (!m_queue.empty()) {
                recordDropLocked(*m_queue.front());
                m_queue.pop_front();
                m_queue.push_back(std::move(block));
                ++m_metrics.enqueuedBlocks;
                m_metrics.enqueuedSamples += sampleCountOf(m_queue.back());
                m_metrics.queueDepth = m_queue.size();
                recordEnqueueLatencyLocked(startedAt);
                notifyWorker = true;
            } else {
                recordDropLocked(*block);
                recordEnqueueLatencyLocked(startedAt);
            }
        } else {
            m_queue.push_back(std::move(block));
            ++m_metrics.enqueuedBlocks;
            m_metrics.enqueuedSamples += sampleCountOf(m_queue.back());
            m_metrics.queueDepth = m_queue.size();
            recordEnqueueLatencyLocked(startedAt);
            notifyWorker = true;
        }
    }

    if (publishOverflow) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "source-to-pipeline bridge RX queue overflow; further drops are counted in metrics");
    }
    if (notifyWorker) {
        m_condition.notify_one();
    }
}

core::OperationResult SourceToPipelineBridge::flush(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        return core::OperationResult::failure("source-to-pipeline flush timeout is invalid");
    }

    std::unique_lock lock(m_mutex);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const bool drained = m_condition.wait_until(lock, deadline, [this] {
        return m_queue.empty() && !m_ingestInProgress;
    });

    if (!drained) {
        return core::OperationResult::failure("source-to-pipeline flush timed out");
    }

    return core::OperationResult::ok();
}

SourceToPipelineBridgeMetrics SourceToPipelineBridge::metrics() const
{
    std::lock_guard lock(m_mutex);
    auto snapshot = m_metrics;
    snapshot.queueDepth = m_queue.size();
    snapshot.queueCapacity = m_config.queueCapacity;
    return snapshot;
}

void SourceToPipelineBridge::clear()
{
    std::lock_guard lock(m_mutex);
    m_queue.clear();
    m_metrics = {};
    m_metrics.queueCapacity = m_config.queueCapacity;
    m_overflowDiagnosticPublished = false;
    m_condition.notify_all();
}

void SourceToPipelineBridge::workerLoop()
{
    while (true) {
        hardware::IBcoStreamSource::SampleBlockPtr block;
        {
            std::unique_lock lock(m_mutex);
            m_condition.wait(lock, [this] {
                return m_stopRequested || !m_queue.empty();
            });

            if (m_queue.empty()) {
                if (m_stopRequested) {
                    break;
                }
                continue;
            }

            block = std::move(m_queue.front());
            m_queue.pop_front();
            m_metrics.queueDepth = m_queue.size();
            m_ingestInProgress = true;
        }
        m_condition.notify_all();

        core::OperationResult ingested =
            core::OperationResult::failure("source-to-pipeline bridge received empty block");
        std::chrono::milliseconds ingestLatency{0};
        const auto sampleCount = sampleCountOf(block);
        if (block && m_pipeline) {
            SignalBlockMetadata metadata;
            metadata.firstSampleIndex = block->stats.firstSampleIndex;
            metadata.lastSampleIndex = block->stats.lastSampleIndex;
            metadata.producedAt = block->stats.producedAt;
            metadata.antennaAzimuthDeg = block->stats.antennaAzimuthDeg;

            const auto startedAt = std::chrono::steady_clock::now();
            ingested = m_pipeline->ingestSamples(block->samples, metadata);
            ingestLatency = elapsedMilliseconds(startedAt);
        }

        {
            std::lock_guard lock(m_mutex);
            if (ingested) {
                ++m_metrics.ingestedBlocks;
                m_metrics.ingestedSamples += sampleCount;
            } else {
                ++m_metrics.rejectedBlocks;
                m_metrics.rejectedSamples += sampleCount;
            }
            m_metrics.ingestLatencyMax =
                std::max(m_metrics.ingestLatencyMax, ingestLatency);
            m_ingestInProgress = false;
        }
        m_condition.notify_all();
    }
}

void SourceToPipelineBridge::recordDropLocked(const hardware::BcoSampleBlock& block)
{
    ++m_metrics.droppedBlocks;
    m_metrics.droppedSamples += static_cast<std::uint64_t>(block.samples.size());
    m_metrics.queueDepth = m_queue.size();
}

void SourceToPipelineBridge::recordEnqueueLatencyLocked(
    std::chrono::steady_clock::time_point startedAt)
{
    m_metrics.enqueueLatencyMax =
        std::max(m_metrics.enqueueLatencyMax, elapsedMilliseconds(startedAt));
}

void SourceToPipelineBridge::publish(infrastructure::DiagnosticSeverity severity,
                                     const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        kSubsystem,
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::pipeline
