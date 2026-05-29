#pragma once

#include "hardware/interfaces/antenna_azimuth_provider.h"
#include "hardware/interfaces/antenna_control.h"

#include <mutex>
#include <optional>

namespace siriusscope::hardware {

class SimulatorAntennaState final : public IAntennaAzimuthProvider
{
public:
    explicit SimulatorAntennaState(double initialAzimuthDeg = 0.0);

    double currentAzimuthDeg() const override;
    void setCurrentAzimuthDeg(double value);

    double targetAzimuthDeg() const;
    void setTargetAzimuthDeg(double value);

    double movementSpeedDegPerSecond() const;
    void setMovementSpeedDegPerSecond(double value);

    bool isMoving() const;
    void setMoving(bool moving);

    std::optional<core::ScanSector> activeScanSector() const;
    std::optional<AntennaSectorScanCommand> activeScanCommand() const;
    void setActiveScanCommand(std::optional<AntennaSectorScanCommand> command);

    std::optional<AntennaManualMoveCommand::Direction> manualMoveDirection() const;
    void setManualMoveDirection(std::optional<AntennaManualMoveCommand::Direction> direction);

    void stop();

private:
    mutable std::mutex m_mutex;
    double m_currentAzimuthDeg = 0.0;
    double m_targetAzimuthDeg = 0.0;
    double m_movementSpeedDegPerSecond = 60.0;
    bool m_moving = false;
    std::optional<AntennaSectorScanCommand> m_activeScanCommand;
    std::optional<AntennaManualMoveCommand::Direction> m_manualMoveDirection;
};

} // namespace siriusscope::hardware
