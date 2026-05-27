#include "hardware/adapters/legacy_bco_sample_source_adapter.h"

#include <algorithm>
#include <utility>

namespace siriusscope::hardware {

LegacyBcoSampleSourceAdapter::LegacyBcoSampleSourceAdapter(IBcoSampleSource* legacySource)
    : m_legacySource(legacySource)
{
}

core::OperationResult LegacyBcoSampleSourceAdapter::configure(const BcoStreamConfig& config)
{
    std::lock_guard lock(m_mutex);
    m_config = config;
    m_metrics = {};
    m_firstBatchAt = {};
    m_configured = true;
    return core::OperationResult::ok();
}

core::OperationResult LegacyBcoSampleSourceAdapter::start(SampleBlockCallback callback)
{
    if (!m_legacySource) {
        return core::OperationResult::failure("legacy BCO sample source is not configured");
    }
    if (!callback) {
        return core::OperationResult::failure("BCO stream callback is not configured");
    }
    {
        std::lock_guard lock(m_mutex);
        if (!m_configured) {
            return core::OperationResult::failure("BCO stream source is not configured");
        }
    }

    return m_legacySource->start([this, callback = std::move(callback)](
                                     const BcoSampleBatch& batch) {
        handleLegacyBatch(batch, callback);
    });
}

core::OperationResult LegacyBcoSampleSourceAdapter::stop()
{
    if (!m_legacySource) {
        return core::OperationResult::ok();
    }

    return m_legacySource->stop();
}

BcoSourceMetrics LegacyBcoSampleSourceAdapter::metrics() const
{
    std::lock_guard lock(m_mutex);
    return m_metrics;
}

void LegacyBcoSampleSourceAdapter::handleLegacyBatch(const BcoSampleBatch& batch,
                                                     const SampleBlockCallback& callback)
{
    auto block = std::make_shared<BcoSampleBlock>();
    block->samples = batch.samples;
    block->stats.sampleCount = static_cast<std::uint64_t>(block->samples.size());
    block->stats.packetCount = block->samples.empty() ? 0U : 1U;
    block->stats.producedAt = std::chrono::steady_clock::now();

    if (!block->samples.empty()) {
        const auto minmax = std::minmax_element(
            block->samples.begin(),
            block->samples.end(),
            [](const core::SignalSample& lhs, const core::SignalSample& rhs) {
                return lhs.sampleIndex < rhs.sampleIndex;
            });
        block->stats.firstSampleIndex = minmax.first->sampleIndex;
        block->stats.lastSampleIndex = minmax.second->sampleIndex;
    }

    updateProducedMetrics(block->stats);
    SampleBlockPtr constBlock = std::move(block);

    const auto callbackStartedAt = std::chrono::steady_clock::now();
    callback(std::move(constBlock));
    const auto callbackDuration = std::chrono::steady_clock::now() - callbackStartedAt;

    updateCallbackDuration(callbackDuration);
}

void LegacyBcoSampleSourceAdapter::updateProducedMetrics(const BcoBatchStats& stats)
{
    std::lock_guard lock(m_mutex);

    if (m_metrics.producedBatches == 0) {
        m_firstBatchAt = stats.producedAt;
    }

    m_metrics.producedSamples += stats.sampleCount;
    ++m_metrics.producedBatches;
    m_metrics.lostPackets += stats.lostPacketCount;
    m_metrics.malformedPackets += stats.malformedPacketCount;

    const auto elapsed = stats.producedAt - m_firstBatchAt;
    const auto elapsedSeconds = std::chrono::duration<double>(elapsed).count();
    if (elapsedSeconds > 0.0) {
        m_metrics.producedSamplesPerSecond =
            static_cast<double>(m_metrics.producedSamples) / elapsedSeconds;
        m_metrics.equivalentMegabytesPerSecond =
            m_metrics.producedSamplesPerSecond * static_cast<double>(sizeof(core::SignalSample))
            / (1024.0 * 1024.0);
    }
}

void LegacyBcoSampleSourceAdapter::updateCallbackDuration(
    std::chrono::steady_clock::duration callbackDuration)
{
    const auto callbackMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(callbackDuration);

    std::lock_guard lock(m_mutex);
    if (callbackMs > m_metrics.maxCallbackDuration) {
        m_metrics.maxCallbackDuration = callbackMs;
    }
}

} // namespace siriusscope::hardware
