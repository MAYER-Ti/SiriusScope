#include "pipeline/processing_engine.h"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace siriusscope::pipeline {

ProcessingEngine::ProcessingEngine(BoundedBlockQueue* queue,
                                   PipelineMetrics* metrics,
                                   PipelineDiagnostics* diagnostics,
                                   WaterfallRowQueue* waterfallRows,
                                   WaterfallAggregatorConfig waterfallConfig,
                                   SnapshotExchange<SpectrumSnapshot>* spectrumSnapshots,
                                   SpectrumAggregatorConfig spectrumConfig,
                                   SnapshotExchange<BearingSnapshot>* bearingSnapshots,
                                   BearingAggregatorConfig bearingConfig)
    : m_queue(queue)
    , m_metrics(metrics)
    , m_diagnostics(diagnostics)
    , m_waterfallRows(waterfallRows)
    , m_spectrumSnapshots(spectrumSnapshots)
    , m_bearingSnapshots(bearingSnapshots)
    , m_waterfallAggregator(std::move(waterfallConfig))
    , m_spectrumAggregator(std::move(spectrumConfig))
    , m_bearingAggregator(std::move(bearingConfig))
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
    m_completedQueuePops = 0;
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        m_waterfallAggregator.reset();
        m_nextWaterfallSourceSnapshotSequenceId = 1;
    }
    {
        std::lock_guard spectrumLock(m_spectrumMutex);
        m_spectrumAggregator.reset();
    }
    {
        std::lock_guard bearingLock(m_bearingMutex);
        m_bearingAggregator.reset();
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
    flushBearingSnapshots();

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
    const auto targetCompletedQueuePops = m_queue ? m_queue->metrics().pushedBlocks : 0;
    {
        std::unique_lock lock(m_mutex);
        const auto isDrained = [this, targetCompletedQueuePops] {
            return (!m_queue || m_queue->metrics().depth == 0)
                && m_completedQueuePops >= targetCompletedQueuePops
                && !m_processingBlock;
        };

        completed = isDrained() || m_flushCondition.wait_for(lock, timeout, isDrained);
    }

    if (!completed) {
        return core::OperationResult::failure("processing flush timed out");
    }

    flushWaterfallRows();
    flushSpectrumSnapshots();
    flushBearingSnapshots();
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
    m_nextWaterfallSourceSnapshotSequenceId = 1;
}

void ProcessingEngine::setSpectrumConfig(SpectrumAggregatorConfig config)
{
    std::lock_guard lock(m_spectrumMutex);
    m_spectrumAggregator.setConfig(std::move(config));
}

void ProcessingEngine::setBearingConfig(BearingAggregatorConfig config)
{
    std::lock_guard lock(m_bearingMutex);
    m_bearingAggregator.setConfig(std::move(config));
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
            ++m_completedQueuePops;
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
    WaterfallAggregatorConfig waterfallConfig;
    const auto waterfallAggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        waterfallResult = m_waterfallAggregator.consume(block);
        waterfallConfig = m_waterfallAggregator.config();
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

    BearingAggregationResult bearingResult;
    const auto bearingAggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard bearingLock(m_bearingMutex);
        bearingResult = m_bearingAggregator.consume(block);
    }
    const auto bearingAggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - bearingAggregationStartedAt);

    const auto finishedAt = std::chrono::steady_clock::now();
    const auto processingLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(finishedAt - startedAt);
    if (m_metrics) {
        m_metrics->recordProcessedBlock(block.sampleCount(), blockAge, processingLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordProcessingLatency(processingLatency);
    }
    publishWaterfallRows(std::move(waterfallResult),
                         std::move(waterfallConfig),
                         waterfallAggregationLatency);
    publishSpectrumSnapshots(std::move(spectrumResult), spectrumAggregationLatency);
    publishBearingSnapshots(std::move(bearingResult), bearingAggregationLatency);

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
                                            WaterfallAggregatorConfig config,
                                            std::chrono::milliseconds aggregationLatency)
{
    WaterfallRowQueuePushResult pushResult;
    const bool hasRows = !result.rows.empty();
    if (hasRows && m_waterfallRows) {
        std::uint64_t sourceSnapshotSequenceId = 0;
        {
            std::lock_guard waterfallLock(m_waterfallMutex);
            sourceSnapshotSequenceId = m_nextWaterfallSourceSnapshotSequenceId++;
        }

        WaterfallRowBatchMetadata metadata;
        metadata.sourceSnapshotSequenceId = sourceSnapshotSequenceId;
        metadata.sourceMinHz = config.sourceMinHz;
        metadata.sourceMaxHz = config.sourceMaxHz;
        metadata.viewMinHz = static_cast<double>(config.sourceMinHz);
        metadata.viewMaxHz = static_cast<double>(config.sourceMaxHz);
        metadata.renderBinCount = config.renderBinCount;
        metadata.rowPeriodNs = config.rowPeriodNs;
        pushResult = m_waterfallRows->pushRows(std::move(result.rows), metadata);
    } else if (hasRows) {
        std::lock_guard waterfallLock(m_waterfallMutex);
        ++m_nextWaterfallSourceSnapshotSequenceId;
    }

    const std::uint64_t producedSnapshots = hasRows ? 1 : 0;
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
        m_diagnostics->recordWaterfallRowQueueOverflow(pushResult.droppedRows,
                                                       pushResult.depth,
                                                       pushResult.capacity);
        m_diagnostics->recordWaterfallRowTimebaseMismatch(
            pushResult.waterfallTimebaseMismatchWarnings,
            pushResult.waterfallRowUtcDeltaMinMs,
            pushResult.waterfallRowUtcDeltaMaxMs,
            pushResult.waterfallExpectedRowPeriodMs);
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

void ProcessingEngine::publishBearingSnapshots(
    BearingAggregationResult result,
    std::chrono::milliseconds aggregationLatency)
{
    const auto producedSnapshots =
        static_cast<std::uint64_t>(result.snapshots.size());
    for (const auto& snapshot : result.snapshots) {
        if (!snapshot) {
            continue;
        }
        if (m_bearingSnapshots) {
            m_bearingSnapshots->publish(snapshot);
        }
    }

    if (m_metrics) {
        m_metrics->recordBearingAggregation(
            producedSnapshots,
            result.deltaCounters.producedEstimates,
            result.deltaCounters.completeCandidates,
            result.deltaCounters.incompleteCandidates,
            result.deltaCounters.missingBeam0Candidates,
            result.deltaCounters.missingBeam1Candidates,
            aggregationLatency);
    }
    if (m_diagnostics) {
        m_diagnostics->recordBearingAggregation(
            result.deltaCounters.completeCandidates,
            result.deltaCounters.incompleteCandidates,
            result.deltaCounters.missingBeam0Candidates,
            result.deltaCounters.missingBeam1Candidates,
            producedSnapshots,
            result.deltaCounters.producedEstimates,
            aggregationLatency);
    }
}

void ProcessingEngine::flushWaterfallRows()
{
    WaterfallAggregationResult result;
    WaterfallAggregatorConfig config;
    const auto aggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard waterfallLock(m_waterfallMutex);
        result = m_waterfallAggregator.flush();
        config = m_waterfallAggregator.config();
    }
    const auto aggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - aggregationStartedAt);
    publishWaterfallRows(std::move(result), std::move(config), aggregationLatency);
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

void ProcessingEngine::flushBearingSnapshots()
{
    BearingAggregationResult result;
    const auto aggregationStartedAt = std::chrono::steady_clock::now();
    {
        std::lock_guard bearingLock(m_bearingMutex);
        result = m_bearingAggregator.flush();
    }
    const auto aggregationLatency =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - aggregationStartedAt);
    publishBearingSnapshots(std::move(result), aggregationLatency);
}

} // namespace siriusscope::pipeline
