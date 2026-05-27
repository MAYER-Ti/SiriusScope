#pragma once

#include "hardware/interfaces/bco_sample_source.h"
#include "hardware/interfaces/bco_stream_source.h"

#include <chrono>
#include <mutex>

namespace siriusscope::hardware {

class LegacyBcoSampleSourceAdapter final : public IBcoStreamSource
{
public:
    explicit LegacyBcoSampleSourceAdapter(IBcoSampleSource* legacySource);

    core::OperationResult configure(const BcoStreamConfig& config) override;
    core::OperationResult start(SampleBlockCallback callback) override;
    core::OperationResult stop() override;
    BcoSourceMetrics metrics() const override;

private:
    void handleLegacyBatch(const BcoSampleBatch& batch, const SampleBlockCallback& callback);
    void updateProducedMetrics(const BcoBatchStats& stats);
    void updateCallbackDuration(std::chrono::steady_clock::duration callbackDuration);

    IBcoSampleSource* m_legacySource = nullptr;
    mutable std::mutex m_mutex;
    BcoStreamConfig m_config;
    BcoSourceMetrics m_metrics;
    std::chrono::steady_clock::time_point m_firstBatchAt{};
    bool m_configured = false;
};

} // namespace siriusscope::hardware
