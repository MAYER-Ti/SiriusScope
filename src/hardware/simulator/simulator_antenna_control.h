#pragma once

#include "hardware/interfaces/antenna_control.h"
#include "hardware/simulator/simulator_antenna_state.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <string>

namespace siriusscope::hardware {

class SimulatorAntennaControl final : public IAntennaControl
{
public:
    explicit SimulatorAntennaControl(
        SimulatorAntennaState* state,
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr);

    core::OperationResult moveToAzimuth(double azimuthDeg) override;
    core::OperationResult startSectorScan(const core::ScanSector& sector) override;
    core::OperationResult stop() override;

private:
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    SimulatorAntennaState* m_state = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
};

} // namespace siriusscope::hardware
