#include "pipeline/data_ingest_pipeline.h"

#include <utility>

namespace siriusscope::pipeline {

DataIngestPipeline::DataIngestPipeline(DataIngestPipelineConfig config,
                                       infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_config(std::move(config))
    , m_diagnosticsSink(diagnosticsSink)
    , m_pool(m_config.blockPool)
    , m_queue(m_config.queueCapacity)
    , m_diagnostics(PipelineDiagnosticsConfig{
          m_config.diagnosticsPublishInterval,
          "DataIngestPipeline",
      })
    , m_engine(&m_queue,
               &m_metrics,
               &m_diagnostics,
               &m_waterfallSnapshots,
               m_config.waterfall,
               &m_spectrumSnapshots,
               m_config.spectrum)
{
}

DataIngestPipeline::~DataIngestPipeline()
{
    stop();
}

core::OperationResult DataIngestPipeline::start()
{
    if (m_running.load(std::memory_order_acquire)) {
        return core::OperationResult::ok();
    }

    m_queue.reset();
    m_metrics.reset();
    m_waterfallSnapshots.reset();
    m_spectrumSnapshots.reset();
    m_accepting.store(m_config.acceptingOnStart, std::memory_order_release);
    const auto started = m_engine.start();
    if (!started) {
        return started;
    }

    m_running.store(true, std::memory_order_release);
    return core::OperationResult::ok();
}

void DataIngestPipeline::stop()
{
    m_accepting.store(false, std::memory_order_release);
    m_queue.shutdown();
    m_engine.stop();
    m_queue.clear();
    m_diagnostics.publishIfDue(m_diagnosticsSink, true);
    m_running.store(false, std::memory_order_release);
}

void DataIngestPipeline::setAccepting(bool accepting) noexcept
{
    m_accepting.store(accepting, std::memory_order_release);
}

bool DataIngestPipeline::accepting() const noexcept
{
    return m_accepting.load(std::memory_order_acquire);
}

bool DataIngestPipeline::running() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

core::OperationResult DataIngestPipeline::ingestSamples(
    std::span<const core::SignalSample> samples,
    SignalBlockMetadata metadata)
{
    if (!running()) {
        recordDroppedBlock(samples.size());
        return core::OperationResult::failure("data ingest pipeline is not running");
    }
    if (!accepting()) {
        recordDroppedBlock(samples.size());
        return core::OperationResult::failure("data ingest pipeline is not accepting samples");
    }
    if (samples.size() > m_config.blockPool.maxSamplesPerBlock) {
        recordDroppedBlock(samples.size());
        return core::OperationResult::failure("signal block exceeds configured block capacity");
    }

    auto block = m_pool.acquire();
    if (!block) {
        recordDroppedBlock(samples.size());
        return core::OperationResult::failure("signal block pool exhausted");
    }

    metadata.sequenceId = m_nextSequenceId.fetch_add(1, std::memory_order_acq_rel);
    if (metadata.producedAt == std::chrono::steady_clock::time_point{}) {
        metadata.producedAt = std::chrono::steady_clock::now();
    }

    block->reset(metadata);
    if (!block->assignSamples(samples)) {
        recordDroppedBlock(samples.size());
        return core::OperationResult::failure("signal block storage rejected sample payload");
    }

    const auto sampleCount = block->sampleCount();
    const auto producedAt = block->producedAt();
    m_metrics.recordInputBlock(sampleCount, producedAt);

    if (!m_queue.tryPush(std::move(block))) {
        recordDroppedBlock(sampleCount);
        m_diagnostics.recordQueueOverflow();
        m_diagnostics.publishIfDue(m_diagnosticsSink);
        return core::OperationResult::failure("bounded block queue is full");
    }

    m_diagnostics.publishIfDue(m_diagnosticsSink);
    return core::OperationResult::ok();
}

core::OperationResult DataIngestPipeline::flushProcessing(std::chrono::milliseconds timeout)
{
    return m_engine.flush(timeout);
}

PipelineMetricsSnapshot DataIngestPipeline::metricsSnapshot() const
{
    return m_metrics.snapshot(m_queue.metrics(), m_pool.counters());
}

ProcessingEngineSummary DataIngestPipeline::lastSummary() const
{
    auto summary = m_engine.lastSummary();
    summary.metrics = metricsSnapshot();
    return summary;
}

std::shared_ptr<const WaterfallSnapshot> DataIngestPipeline::latestWaterfallSnapshot() const
{
    return m_waterfallSnapshots.latest();
}

std::shared_ptr<const SpectrumSnapshot> DataIngestPipeline::latestSpectrumSnapshot() const
{
    return m_spectrumSnapshots.latest();
}

void DataIngestPipeline::configureWaterfall(WaterfallAggregatorConfig config)
{
    m_config.waterfall = config;
    m_waterfallSnapshots.reset();
    m_engine.setWaterfallConfig(std::move(config));
}

void DataIngestPipeline::configureSpectrum(SpectrumAggregatorConfig config)
{
    m_config.spectrum = config;
    m_spectrumSnapshots.reset();
    m_engine.setSpectrumConfig(std::move(config));
}

void DataIngestPipeline::clearQueuedBlocks()
{
    m_queue.clear();
}

void DataIngestPipeline::recordDroppedBlock(std::size_t sampleCount)
{
    m_metrics.recordDroppedBlock(sampleCount);
    m_diagnostics.recordDroppedBlock(static_cast<std::uint64_t>(sampleCount));
    m_diagnostics.publishIfDue(m_diagnosticsSink);
}

} // namespace siriusscope::pipeline
