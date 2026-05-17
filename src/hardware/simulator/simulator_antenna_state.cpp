#include "hardware/simulator/simulator_antenna_state.h"

#include <cmath>

namespace siriusscope::hardware {

namespace {

double normalizeAzimuth(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }

    auto normalized = std::fmod(value, core::DomainConstraints::maxAzimuthDeg);
    if (normalized < core::DomainConstraints::minAzimuthDeg) {
        normalized += core::DomainConstraints::maxAzimuthDeg;
    }

    return normalized;
}

} // namespace

SimulatorAntennaState::SimulatorAntennaState(double initialAzimuthDeg)
    : m_currentAzimuthDeg(normalizeAzimuth(initialAzimuthDeg))
    , m_targetAzimuthDeg(normalizeAzimuth(initialAzimuthDeg))
{
}

double SimulatorAntennaState::currentAzimuthDeg() const
{
    std::lock_guard lock(m_mutex);
    return m_currentAzimuthDeg;
}

void SimulatorAntennaState::setCurrentAzimuthDeg(double value)
{
    std::lock_guard lock(m_mutex);
    m_currentAzimuthDeg = normalizeAzimuth(value);
}

double SimulatorAntennaState::targetAzimuthDeg() const
{
    std::lock_guard lock(m_mutex);
    return m_targetAzimuthDeg;
}

void SimulatorAntennaState::setTargetAzimuthDeg(double value)
{
    std::lock_guard lock(m_mutex);
    m_targetAzimuthDeg = normalizeAzimuth(value);
}

double SimulatorAntennaState::movementSpeedDegPerSecond() const
{
    std::lock_guard lock(m_mutex);
    return m_movementSpeedDegPerSecond;
}

void SimulatorAntennaState::setMovementSpeedDegPerSecond(double value)
{
    std::lock_guard lock(m_mutex);
    m_movementSpeedDegPerSecond = value;
}

bool SimulatorAntennaState::isMoving() const
{
    std::lock_guard lock(m_mutex);
    return m_moving;
}

void SimulatorAntennaState::setMoving(bool moving)
{
    std::lock_guard lock(m_mutex);
    m_moving = moving;
}

std::optional<core::ScanSector> SimulatorAntennaState::activeScanSector() const
{
    std::lock_guard lock(m_mutex);
    return m_activeScanCommand ? std::optional<core::ScanSector>(m_activeScanCommand->requestedSector)
                               : std::nullopt;
}

std::optional<AntennaSectorScanCommand> SimulatorAntennaState::activeScanCommand() const
{
    std::lock_guard lock(m_mutex);
    return m_activeScanCommand;
}

void SimulatorAntennaState::setActiveScanCommand(std::optional<AntennaSectorScanCommand> command)
{
    std::lock_guard lock(m_mutex);
    m_activeScanCommand = command;
    if (m_activeScanCommand) {
        m_manualMoveDirection.reset();
    }
}

std::optional<AntennaManualMoveCommand::Direction> SimulatorAntennaState::manualMoveDirection() const
{
    std::lock_guard lock(m_mutex);
    return m_manualMoveDirection;
}

void SimulatorAntennaState::setManualMoveDirection(
    std::optional<AntennaManualMoveCommand::Direction> direction)
{
    std::lock_guard lock(m_mutex);
    m_manualMoveDirection = direction;
    if (m_manualMoveDirection) {
        m_activeScanCommand.reset();
    }
}

void SimulatorAntennaState::stop()
{
    std::lock_guard lock(m_mutex);
    m_moving = false;
    m_targetAzimuthDeg = m_currentAzimuthDeg;
    m_activeScanCommand.reset();
    m_manualMoveDirection.reset();
}

} // namespace siriusscope::hardware
