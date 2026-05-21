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
    return core::OperationResult::ok();
}

core::OperationResult LegacyBcoSampleSourceAdapter::start(SampleBlockCallback callback)
{
    if (!m_legacySource) {
        return core::OperationResult::failure("legacy BCO sample source is not set");
    }
    if (!callback) {
        return core::OperationResult::failure("sample block callback must not be empty");
    }

    return m_legacySource->start([this, callback = std::move(callback)](
                                     const BcoSampleBatch& batch) {
        handleLegacyBatch(batch, callback);
    });
}

core::OperationResult LegacyBcoSampleSourceAdapter::stop()
{
    if (!m_legacySource) {
        return core::OperationResult::failure("legacy BCO sample source is not set");
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

    const auto callbackStartedAt = std::chrono::steady_clock::now();
    updateProducedMetrics(block->stats);
    callback(std::const_pointer_cast<const BcoSampleBlock>(block));
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
            / 1'000'000.0;
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
