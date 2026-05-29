#include "pipeline/processing_engine.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace siriusscope::pipeline {

ProcessingEngine::ProcessingEngine(BoundedBlockQueue* queue,
                                   PipelineMetrics* metrics,
                                   PipelineDiagnostics* diagnostics,
                                   SnapshotExchange<WaterfallSnapshot>* waterfallSnapshots,
                                   WaterfallAggregatorConfig waterfallConfig,
                                   SnapshotExchange<SpectrumSnapshot>* spectrumSnapshots,
                                   SpectrumAggregatorConfig spectrumConfig)
    : m_queue(queue)
    , m_metrics(metrics)
    , m_diagnostics(diagnostics)
    , m_waterfallSnapshots(waterfallSnapshots)
    , m_spectrumSnapshots(spectrumSnapshots)
    , m_waterfallAggregator(std::move(waterfallConfig))
    , m_spectrumAggregator(std::move(spectrumConfig))
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
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        m_waterfallAggregator.reset();
    }
    {
        std::lock_guard spectrumLock(m_spectrumMutex);
        m_spectrumAggregator.reset();
    }
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

    flushWaterfallRows();
    flushSpectrumSnapshots();

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

    bool completed = false;
    {
        std::unique_lock lock(m_mutex);
        const auto isDrained = [this] {
            return (!m_queue || m_queue->metrics().depth == 0) && !m_processingBlock;
        };

        completed = isDrained() || m_flushCondition.wait_for(lock, timeout, isDrained);
    }

    if (!completed) {
        return core::OperationResult::failure("processing flush timed out");
    }

    flushWaterfallRows();
    flushSpectrumSnapshots();
    return core::OperationResult::ok();
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

void ProcessingEngine::setWaterfallConfig(WaterfallAggregatorConfig config)
{
    std::lock_guard lock(m_waterfallMutex);
    m_waterfallAggregator.setConfig(std::move(config));
}

void ProcessingEngine::setSpectrumConfig(SpectrumAggregatorConfig config)
{
    std::lock_guard lock(m_spectrumMutex);
    m_spectrumAggregator.setConfig(std::move(config));
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

    WaterfallAggregationResult waterfallResult;
    const auto waterfallAggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        waterfallResult = m_waterfallAggregator.consume(block);
    }
    const auto waterfallAggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - waterfallAggregationStartedAt);

    SpectrumAggregationResult spectrumResult;
    const auto spectrumAggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard spectrumLock(m_spectrumMutex);
        spectrumResult = m_spectrumAggregator.consume(block);
    }
    const auto spectrumAggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - spectrumAggregationStartedAt);

    const auto finishedAt = std::chrono::steady_clock::now();
    const auto processingLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt - startedAt);
    if (m_metrics) {
        m_metrics->recordProcessedBlock(block.sampleCount(), blockAge, processingLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordProcessingLatency(processingLatency);
    }
    publishWaterfallRows(std::move(waterfallResult), waterfallAggregationLatency);
    publishSpectrumSnapshots(std::move(spectrumResult), spectrumAggregationLatency);

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

void ProcessingEngine::publishWaterfallRows(WaterfallAggregationResult result,
                                            std::chrono::milliseconds aggregationLatency)
{
    std::uint64_t producedSnapshots = 0;
    std::shared_ptr<const WaterfallSnapshot> snapshot;
    if (!result.rows.empty()) {
        std::lock_guard waterfallLock(m_waterfallMutex);
        snapshot = m_waterfallAggregator.makeSnapshot(std::move(result.rows));
    }

    if (snapshot) {
        producedSnapshots = 1;
        if (m_waterfallSnapshots) {
            m_waterfallSnapshots->publish(snapshot);
        }
    }

    if (m_metrics) {
        m_metrics->recordWaterfallAggregation(result.deltaCounters.producedRows,
                                              producedSnapshots,
                                              result.deltaCounters.invalidFrequencySamples,
                                              result.deltaCounters.outOfRangeSamples,
                                              result.deltaCounters.emptyBlocks,
                                              aggregationLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordWaterfallAggregation(
            result.deltaCounters.invalidFrequencySamples,
            result.deltaCounters.outOfRangeSamples,
            result.deltaCounters.emptyBlocks,
            result.deltaCounters.producedRows,
            producedSnapshots,
            aggregationLatency);
    }
}

void ProcessingEngine::publishSpectrumSnapshots(
    SpectrumAggregationResult result,
    std::chrono::milliseconds aggregationLatency)
{
    const auto producedSnapshots =
        static_cast<std::uint64_t>(result.snapshots.size());
    std::uint64_t latestSequence = 0;
    for (const auto& snapshot : result.snapshots) {
        if (!snapshot) {
            continue;
        }
        latestSequence = snapshot->sequenceId;
        if (m_spectrumSnapshots) {
            m_spectrumSnapshots->publish(snapshot);
        }
    }

    if (m_metrics) {
        m_metrics->recordSpectrumAggregation(producedSnapshots,
                                             latestSequence,
                                             result.deltaCounters.invalidSamples,
                                             result.deltaCounters.outOfRangeSamples,
                                             aggregationLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordSpectrumAggregation(result.deltaCounters.invalidSamples,
                                                result.deltaCounters.outOfRangeSamples,
                                                producedSnapshots,
                                                aggregationLatency);
    }
}

void ProcessingEngine::flushWaterfallRows()
{
    WaterfallAggregationResult result;
    const auto aggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        result = m_waterfallAggregator.flush();
    }
    const auto aggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - aggregationStartedAt);
    publishWaterfallRows(std::move(result), aggregationLatency);
}

void ProcessingEngine::flushSpectrumSnapshots()
{
    SpectrumAggregationResult result;
    const auto aggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard spectrumLock(m_spectrumMutex);
        result = m_spectrumAggregator.flush();
    }
    const auto aggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - aggregationStartedAt);
    publishSpectrumSnapshots(std::move(result), aggregationLatency);
}

} // namespace siriusscope::pipeline
