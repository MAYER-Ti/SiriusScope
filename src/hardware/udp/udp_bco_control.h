#pragma once
#include "hardware/interfaces/bco_control.h"
#include "hardware/interfaces/bco_stream_source.h"

namespace siriusscope::hardware {
//! Stages receiver configuration; the RX worker sends Subscribe/Stop on the wire.
//! Success here means locally accepted, not a remote hardware acknowledgement.
class UdpBcoControl final : public IBcoControl {
public:
    explicit UdpBcoControl(IBcoStreamSource* source) : m_source(source) {}
    core::OperationResult applyBandConfig(const core::BandConfig& config) override;
    core::OperationResult applyBandConfigs(const std::vector<core::BandConfig>& configs) override;
    core::OperationResult startProcessing(const BcoProcessingStartCommand& command) override;
    core::OperationResult stopProcessing() override { return core::OperationResult::ok(); }
private:
    IBcoStreamSource* m_source;
    std::vector<core::BandConfig> m_bands;
};
}
