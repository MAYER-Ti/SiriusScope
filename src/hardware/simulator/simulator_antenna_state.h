#pragma once

#include "core/domain_models.h"

#include <mutex>
#include <optional>

namespace siriusscope::hardware {

class SimulatorAntennaState
{
public:
    explicit SimulatorAntennaState(double initialAzimuthDeg = 0.0);

    double currentAzimuthDeg() const;
    void setCurrentAzimuthDeg(double value);

    double targetAzimuthDeg() const;
    void setTargetAzimuthDeg(double value);

    bool isMoving() const;
    void setMoving(bool moving);

    std::optional<core::ScanSector> activeScanSector() const;
    void setActiveScanSector(std::optional<core::ScanSector> sector);

    void stop();

private:
    mutable std::mutex m_mutex;
    double m_currentAzimuthDeg = 0.0;
    double m_targetAzimuthDeg = 0.0;
    bool m_moving = false;
    std::optional<core::ScanSector> m_activeScanSector;
};

} // namespace siriusscope::hardware
