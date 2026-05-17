#pragma once

#include "core/domain_models.h"

namespace siriusscope::core {

struct ScanMotionOptions
{
    double speedDegPerSec = 10.0;
    double minSectorDeg = 5.0;
    double angleToleranceDeg = 0.2;
};

struct PlannedScanPath
{
    ScanSector requestedSector;
    double startAzimuthDeg = 0.0;
    double endAzimuthDeg = 0.0;
    double safeStartCoordDeg = 0.0;
    double safeEndCoordDeg = 0.0;
    double spanDeg = 0.0;
    bool crossesNorthDeg = false;
};

class AntennaMotionPlanner
{
public:
    static constexpr double blindZoneStartDeg = 170.0;
    static constexpr double blindZoneEndDeg = 190.0;
    static constexpr double maxSafeCoordDeg = 340.0;

    static DomainResult<PlannedScanPath> planSectorScan(
        double leftAngleDeg,
        double rightAngleDeg,
        const ScanMotionOptions& options = {});

    static double normalizeAzimuth(double azimuthDeg) noexcept;
    static bool isInBlindZone(double azimuthDeg) noexcept;
    static double clampToSafeAngle(double azimuthDeg) noexcept;
    static double toSafeCoord(double azimuthDeg) noexcept;
    static double fromSafeCoord(double safeCoordDeg) noexcept;
};

} // namespace siriusscope::core
