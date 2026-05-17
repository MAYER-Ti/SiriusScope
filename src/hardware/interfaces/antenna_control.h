#pragma once

#include "core/antenna_motion_planner.h"
#include "core/domain_models.h"
#include "core/operation_result.h"

namespace siriusscope::hardware {

struct AntennaSectorScanCommand
{
    core::ScanSector requestedSector;
    double startAzimuthDeg = 0.0;
    double endAzimuthDeg = 0.0;
    double safeStartCoordDeg = 0.0;
    double safeEndCoordDeg = 0.0;
    double speedDegPerSec = 10.0;
    core::ScanDirection direction = core::ScanDirection::IncreasingSafeCoord;
};

struct AntennaManualMoveCommand
{
    enum class Direction {
        Left,
        Right,
    };

    Direction direction = Direction::Right;
    double speedDegPerSec = 10.0;
};

class IAntennaControl
{
public:
    virtual ~IAntennaControl() = default;

    virtual core::OperationResult moveToAzimuth(double azimuthDeg) = 0;
    virtual core::OperationResult startSectorScan(const AntennaSectorScanCommand& command) = 0;
    virtual core::OperationResult startManualMove(const AntennaManualMoveCommand& command) = 0;
    virtual core::OperationResult stop() = 0;
};

class StubAntennaControl final : public IAntennaControl
{
public:
    core::OperationResult moveToAzimuth(double) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult startSectorScan(const AntennaSectorScanCommand&) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult startManualMove(const AntennaManualMoveCommand&) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        return core::OperationResult::ok();
    }
};

} // namespace siriusscope::hardware
