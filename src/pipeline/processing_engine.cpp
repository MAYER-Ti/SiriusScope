#include "pipeline/processing_engine.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace siriusscope::pipeline {

ProcessingEngine::ProcessingEngine(BoundedBlockQueue* queue,
                                   PipelineMetrics* metrics,
                                   PipelineDiagnostics* diagnostics)
    : m_queue(queue)
    , m_metrics(metrics)
    , m_diagnostics(diagnostics)
{
}

ProcessingEngine::~ProcessingEngine()
{
    stop();
}

core::OperationResult ProcessingEngine::start()
{
    std::lock_guard lock(m_mutex);
    if (m_running) {
        return core::OperationResult::ok();
    }
    if (!m_queue || !m_metrics) {
        return core::OperationResult::failure("processing engine dependencies are not configured");
    }

    m_running = true;
    m_processingBlock = false;
    m_summary = {};
    m_worker = std::thread(&ProcessingEngine::workerLoop, this);
    return core::OperationResult::ok();
}

void ProcessingEngine::stop()
{
    if (m_queue) {
        m_queue->shutdown();
    }

    {
        std::lock_guard lock(m_mutex);
        if (!m_running && !m_worker.joinable()) {
            return;
        }
        m_running = false;
    }

    m_flushCondition.notify_all();
    if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id()) {
        m_worker.join();
    }

    {
        std::lock_guard lock(m_mutex);
        m_processingBlock = false;
    }
    m_flushCondition.notify_all();
}

core::OperationResult ProcessingEngine::flush(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        return core::OperationResult::failure("processing flush timeout is invalid");
    }

    std::unique_lock lock(m_mutex);
    const auto isDrained = [this] {
        return (!m_queue || m_queue->metrics().depth == 0) && !m_processingBlock;
    };

    if (isDrained()) {
        return core::OperationResult::ok();
    }

    const bool completed = m_flushCondition.wait_for(lock, timeout, isDrained);
    return completed ? core::OperationResult::ok()
                     : core::OperationResult::failure("processing flush timed out");
}

ProcessingEngineSummary ProcessingEngine::lastSummary() const
{
    std::lock_guard lock(m_mutex);
    return m_summary;
}

bool ProcessingEngine::running() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_running;
}

void ProcessingEngine::workerLoop()
{
    for (;;) {
        auto block = m_queue ? m_queue->pop() : std::nullopt;
        if (!block) {
            break;
        }

        {
            std::lock_guard lock(m_mutex);
            m_processingBlock = true;
        }

        processBlock(**block);
        block->reset();

        {
            std::lock_guard lock(m_mutex);
            m_processingBlock = false;
        }
        m_flushCondition.notify_all();
    }

    {
        std::lock_guard lock(m_mutex);
        m_running = false;
        m_processingBlock = false;
    }
    m_flushCondition.notify_all();
}

void ProcessingEngine::processBlock(const SignalBlock& block)
{
    const auto startedAt = std::chrono::steady_clock::now();
    const auto producedAt = block.producedAt();
    const auto blockAge = producedAt == std::chrono::steady_clock::time_point{}
        ? std::chrono::milliseconds{0}
        : std::chrono::duration_cast<std::chrono::milliseconds>(startedAt - producedAt);

    std::map<std::pair<int, int>, BandBeamAggregateSummary> aggregates;
    std::uint64_t firstSampleIndex = block.firstSampleIndex();
    std::uint64_t lastSampleIndex = block.lastSampleIndex();
    bool sawSample = false;

    for (const auto& sample : block.samples()) {
        if (!sawSample) {
            firstSampleIndex = sample.sampleIndex;
            lastSampleIndex = sample.sampleIndex;
            sawSample = true;
        } else {
            firstSampleIndex = std::min(firstSampleIndex, sample.sampleIndex);
            lastSampleIndex = std::max(lastSampleIndex, sample.sampleIndex);
        }

        auto& summary = aggregates[{sample.bandIndex, sample.beamIndex}];
        summary.bandIndex = sample.bandIndex;
        summary.beamIndex = sample.beamIndex;
        ++summary.sampleCount;
        summary.maxAmplitude = std::max(summary.maxAmplitude, sample.amplitude);
    }

    const auto finishedAt = std::chrono::steady_clock::now();
    const auto processingLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt - startedAt);
    if (m_metrics) {
        m_metrics->recordProcessedBlock(block.sampleCount(), blockAge, processingLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordProcessingLatency(processingLatency);
    }

    std::lock_guard lock(m_mutex);
    ++m_summary.processedBlocks;
    m_summary.processedSamples += static_cast<std::uint64_t>(block.sampleCount());
    if (sawSample || block.sampleCount() == 0) {
        if (m_summary.processedBlocks == 1) {
            m_summary.firstSampleIndex = firstSampleIndex;
            m_summary.lastSampleIndex = lastSampleIndex;
        } else {
            m_summary.firstSampleIndex =
                std::min(m_summary.firstSampleIndex, firstSampleIndex);
            m_summary.lastSampleIndex =
                std::max(m_summary.lastSampleIndex, lastSampleIndex);
        }
    }

    for (const auto& [key, value] : aggregates) {
        const auto found =
            std::find_if(m_summary.bandBeamSummaries.begin(),
                         m_summary.bandBeamSummaries.end(),
                         [&value](const auto& existing) {
                             return existing.bandIndex == value.bandIndex
                                 && existing.beamIndex == value.beamIndex;
                         });
        if (found == m_summary.bandBeamSummaries.end()) {
            m_summary.bandBeamSummaries.push_back(value);
        } else {
            found->sampleCount += value.sampleCount;
            found->maxAmplitude = std::max(found->maxAmplitude, value.maxAmplitude);
        }
    }
}

} // namespace siriusscope::pipeline
