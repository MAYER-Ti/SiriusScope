#include "hardware/simulator/simulator_antenna_control.h"

#include "core/antenna_motion_planner.h"

#include <chrono>
#include <cmath>
#include <optional>
#include <string>

namespace siriusscope::hardware {

namespace {

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

bool isValidSpeed(double speedDegPerSec)
{
    return std::isfinite(speedDegPerSec) && speedDegPerSec > 0.0;
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

    if (core::AntennaMotionPlanner::isInBlindZone(azimuthDeg)) {
        const auto message = "azimuth is inside antenna blind zone 170..190 degrees";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    m_state->setActiveScanCommand(std::nullopt);
    m_state->setManualMoveDirection(std::nullopt);
    m_state->setTargetAzimuthDeg(azimuthDeg);
    m_state->setMoving(true);

    publish(infrastructure::DiagnosticSeverity::Info,
            "antenna simulator accepted move command to " + std::to_string(azimuthDeg));
    return core::OperationResult::ok();
}

core::OperationResult SimulatorAntennaControl::startSectorScan(
    const AntennaSectorScanCommand& command)
{
    if (!m_state) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "antenna simulator rejected sector scan: state is not available");
        return core::OperationResult::failure("antenna simulator state is not available");
    }

    auto validation = command.requestedSector.validate();
    validation.merge(core::validateAzimuth(command.startAzimuthDeg));
    validation.merge(core::validateAzimuth(command.endAzimuthDeg));
    if (!validation) {
        const auto message = "invalid scan sector: " + validationMessage(validation);
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    if (!isValidSpeed(command.speedDegPerSec)) {
        const auto message = "scan speed must be positive";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    if (core::AntennaMotionPlanner::isInBlindZone(command.startAzimuthDeg)
        || core::AntennaMotionPlanner::isInBlindZone(command.endAzimuthDeg)) {
        const auto message = "scan sector endpoint is inside antenna blind zone 170..190 degrees";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    if (command.safeStartCoordDeg < 0.0
        || command.safeStartCoordDeg > core::AntennaMotionPlanner::maxSafeCoordDeg
        || command.safeEndCoordDeg < 0.0
        || command.safeEndCoordDeg > core::AntennaMotionPlanner::maxSafeCoordDeg
        || std::abs(command.safeStartCoordDeg - command.safeEndCoordDeg) <= 0.001) {
        const auto message = "scan sector safe path is invalid";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    m_state->setMovementSpeedDegPerSecond(command.speedDegPerSec);
    m_state->setActiveScanCommand(command);
    m_state->setTargetAzimuthDeg(command.endAzimuthDeg);
    m_state->setMoving(true);

    publish(infrastructure::DiagnosticSeverity::Info,
            "antenna simulator accepted sector scan command");
    return core::OperationResult::ok();
}

core::OperationResult SimulatorAntennaControl::startManualMove(
    const AntennaManualMoveCommand& command)
{
    if (!m_state) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "antenna simulator rejected manual move: state is not available");
        return core::OperationResult::failure("antenna simulator state is not available");
    }

    if (!isValidSpeed(command.speedDegPerSec)) {
        const auto message = "manual move speed must be positive";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    const auto current = m_state->currentAzimuthDeg();
    if (core::AntennaMotionPlanner::isInBlindZone(current)) {
        const auto message = "current azimuth is inside antenna blind zone 170..190 degrees";
        publish(infrastructure::DiagnosticSeverity::Warning, message);
        return core::OperationResult::failure(message);
    }

    m_state->setMovementSpeedDegPerSecond(command.speedDegPerSec);
    m_state->setManualMoveDirection(command.direction);
    m_state->setTargetAzimuthDeg(
        command.direction == AntennaManualMoveCommand::Direction::Left
            ? core::AntennaMotionPlanner::blindZoneEndDeg
            : core::AntennaMotionPlanner::blindZoneStartDeg);
    m_state->setMoving(true);

    publish(infrastructure::DiagnosticSeverity::Info,
            "antenna simulator accepted manual move command");
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
