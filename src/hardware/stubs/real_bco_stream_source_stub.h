#pragma once

#include "hardware/interfaces/bco_stream_source.h"

#include <mutex>

namespace siriusscope::hardware {

class RealBcoStreamSourceStub final : public IBcoStreamSource
{
public:
    core::OperationResult configure(const BcoStreamConfig& config) override;
    core::OperationResult start(SampleBlockCallback callback) override;
    core::OperationResult stop() override;
    BcoSourceMetrics metrics() const override;

private:
    mutable std::mutex m_mutex;
    BcoStreamConfig m_config;
};

} // namespace siriusscope::hardware
