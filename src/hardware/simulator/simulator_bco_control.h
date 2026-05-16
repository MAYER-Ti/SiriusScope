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

private:
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    SimulatorBcoSampleSource* m_sampleSource = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
};

} // namespace siriusscope::hardware
