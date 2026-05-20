#pragma once

#include "hardware/interfaces/bco_control.h"
#include "hardware/simulator/simulator_bco_sample_source.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <string>

namespace siriusscope::hardware {

class SimulatorBcoControl final : public IBcoControl
{
public:
    explicit SimulatorBcoControl(
        SimulatorBcoSampleSource* sampleSource,
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr);

    core::OperationResult applyBandConfig(const core::BandConfig& config) override;
    core::OperationResult applyBandConfigs(const std::vector<core::BandConfig>& configs) override;
    core::OperationResult startProcessing(const BcoProcessingStartCommand& command) override;
    core::OperationResult stopProcessing() override;
    BcoProcessingState processingState() const noexcept { return m_processingState; }

private:
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    SimulatorBcoSampleSource* m_sampleSource = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    BcoProcessingState m_processingState = BcoProcessingState::Idle;
};

} // namespace siriusscope::hardware
