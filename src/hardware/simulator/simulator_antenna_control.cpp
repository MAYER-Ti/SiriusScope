#include "hardware/simulator/simulator_antenna_control.h"

#include <chrono>
#include <cmath>
#include <string>

namespace siriusscope::hardware {

namespace {

constexpr double kBlindStartDeg = 170.0;
constexpr double kBlindEndDeg = 190.0;
constexpr double kBlindMiddleDeg = 180.0;

bool isInBlindZone(double azimuthDeg)
{
    return azimuthDeg > kBlindStartDeg && azimuthDeg < kBlindEndDeg;
}

bool sectorCrossesBlindZone(const core::ScanSector& sector)
{
    return sector.contains(kBlindMiddleDeg);
}

std::string validationMessage(const core::ValidationResult& validation)
{
    std::string message;
    for (const auto& issue : validation.issues()) {
        if (!message.empty()) {
            message += "; ";
        }
        message += issue.message.empty() ? "domain validation error" : issue.message;
    }

    return message.empty() ? "invalid antenna command" : message;
}

} // namespace

SimulatorAntennaControl::SimulatorAntennaControl(
    SimulatorAntennaState* state,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_state(state)
    , m_diagnosticsSink(diagnosticsSink)
{
}

core::OperationResult SimulatorAntennaControl::moveToAzimuth(double azimuthDeg)
{
    if (!m_state) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "antenna simulator rejected move command: state is not available");
        return core::OperationResult::failure("antenna simulator state is not available");
    }

    const auto validation = core::validateAzimuth(azimuthDeg);
    if (!validation) {
        const auto message = "invalid azimuth: " + validationMessage(validation);
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    if (isInBlindZone(azimuthDeg)) {
        const auto message = "azimuth is inside antenna blind zone 170..190 degrees";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    m_state->setTargetAzimuthDeg(azimuthDeg);
    m_state->setMoving(true);

    publish(infrastructure::DiagnosticSeverity::Info,
            "antenna simulator accepted move command to " + std::to_string(azimuthDeg));
    return core::OperationResult::ok();
}

core::OperationResult SimulatorAntennaControl::startSectorScan(const core::ScanSector& sector)
{
    if (!m_state) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "antenna simulator rejected sector scan: state is not available");
        return core::OperationResult::failure("antenna simulator state is not available");
    }

    const auto validation = sector.validate();
    if (!validation) {
        const auto message = "invalid scan sector: " + validationMessage(validation);
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    if (isInBlindZone(sector.startAzimuthDeg) || isInBlindZone(sector.endAzimuthDeg)) {
        const auto message = "scan sector endpoint is inside antenna blind zone 170..190 degrees";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    if (sectorCrossesBlindZone(sector)) {
        const auto message = "scan sector crosses antenna blind zone 170..190 degrees";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    m_state->setActiveScanSector(sector);
    m_state->setCurrentAzimuthDeg(sector.startAzimuthDeg);
    m_state->setTargetAzimuthDeg(sector.endAzimuthDeg);
    m_state->setMoving(true);

    publish(infrastructure::DiagnosticSeverity::Info,
            "antenna simulator accepted sector scan command");
    return core::OperationResult::ok();
}

core::OperationResult SimulatorAntennaControl::stop()
{
    if (m_state) {
        m_state->stop();
    }

    publish(infrastructure::DiagnosticSeverity::Info, "antenna simulator stopped");
    return core::OperationResult::ok();
}

void SimulatorAntennaControl::publish(infrastructure::DiagnosticSeverity severity,
                                      const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SimulatorAntennaControl",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::hardware
