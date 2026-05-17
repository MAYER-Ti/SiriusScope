#include "core/antenna_motion_planner.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace siriusscope::core {
namespace {

bool isFinite(double value)
{
    return std::isfinite(value);
}

DomainResult<PlannedScanPath> failure(std::string message)
{
    auto validation = ValidationResult::invalid(ValidationCode::InvalidScanSector,
                                                std::move(message));
    return DomainResult<PlannedScanPath>::failure(std::move(validation));
}

} // namespace

DomainResult<PlannedScanPath> AntennaMotionPlanner::planSectorScan(
    double leftAngleDeg,
    double rightAngleDeg,
    const ScanMotionOptions& options)
{
    ValidationResult validation;
    validation.merge(validateAzimuth(leftAngleDeg));
    validation.merge(validateAzimuth(rightAngleDeg));

    if (!isFinite(options.speedDegPerSec) || options.speedDegPerSec <= 0.0) {
        validation.add(ValidationCode::InvalidScanSector,
                       "scan speed must be positive");
    }
    if (!isFinite(options.minSectorDeg) || options.minSectorDeg <= 0.0) {
        validation.add(ValidationCode::InvalidScanSector,
                       "minimum scan sector width must be positive");
    }
    if (!isFinite(options.angleToleranceDeg) || options.angleToleranceDeg < 0.0) {
        validation.add(ValidationCode::InvalidScanSector,
                       "angle tolerance must be non-negative");
    }

    if (!validation) {
        return DomainResult<PlannedScanPath>::failure(std::move(validation));
    }

    if (isInBlindZone(leftAngleDeg) || isInBlindZone(rightAngleDeg)) {
        return failure("scan sector endpoint is inside antenna blind zone 170..190 degrees");
    }

    const auto leftCoord = toSafeCoord(leftAngleDeg);
    const auto rightCoord = toSafeCoord(rightAngleDeg);
    const auto safeStartCoord = std::min(leftCoord, rightCoord);
    const auto safeEndCoord = std::max(leftCoord, rightCoord);
    const auto span = safeEndCoord - safeStartCoord;

    if (span <= options.angleToleranceDeg) {
        return failure("scan sector width must be positive");
    }
    if (span + options.angleToleranceDeg < options.minSectorDeg) {
        return failure("scan sector width is below configured minimum");
    }

    const auto startAzimuth = fromSafeCoord(safeStartCoord);
    const auto endAzimuth = fromSafeCoord(safeEndCoord);
    auto sector = ScanSector::create(startAzimuth, endAzimuth);
    if (!sector) {
        return DomainResult<PlannedScanPath>::failure(sector.validation());
    }

    PlannedScanPath path;
    path.requestedSector = *sector.value();
    path.startAzimuthDeg = startAzimuth;
    path.endAzimuthDeg = endAzimuth;
    path.safeStartCoordDeg = safeStartCoord;
    path.safeEndCoordDeg = safeEndCoord;
    path.spanDeg = span;
    path.direction = ScanDirection::IncreasingSafeCoord;
    path.crossesNorthDeg = path.requestedSector.isWrapAround();
    return DomainResult<PlannedScanPath>::success(path);
}

DomainResult<PlannedScanPath> AntennaMotionPlanner::planSectorScanFromCurrentAzimuth(
    double leftAngleDeg,
    double rightAngleDeg,
    double currentAzimuthDeg,
    const ScanMotionOptions& options)
{
    const auto basePath = planSectorScan(leftAngleDeg, rightAngleDeg, options);
    if (!basePath) {
        return DomainResult<PlannedScanPath>::failure(basePath.validation());
    }

    auto path = *basePath.value();
    const auto minCoord = path.safeStartCoordDeg;
    const auto maxCoord = path.safeEndCoordDeg;
    const auto currentCoord = toSafeCoord(currentAzimuthDeg);
    const auto distanceToMin = std::abs(currentCoord - minCoord);
    const auto distanceToMax = std::abs(currentCoord - maxCoord);

    if (distanceToMin <= distanceToMax) {
        path.safeStartCoordDeg = minCoord;
        path.safeEndCoordDeg = maxCoord;
        path.direction = ScanDirection::IncreasingSafeCoord;
    } else {
        path.safeStartCoordDeg = maxCoord;
        path.safeEndCoordDeg = minCoord;
        path.direction = ScanDirection::DecreasingSafeCoord;
    }

    path.startAzimuthDeg = fromSafeCoord(path.safeStartCoordDeg);
    path.endAzimuthDeg = fromSafeCoord(path.safeEndCoordDeg);
    path.spanDeg = std::abs(path.safeEndCoordDeg - path.safeStartCoordDeg);
    path.crossesNorthDeg = path.requestedSector.isWrapAround();
    return DomainResult<PlannedScanPath>::success(path);
}

double AntennaMotionPlanner::normalizeAzimuth(double azimuthDeg) noexcept
{
    if (!std::isfinite(azimuthDeg)) {
        return 0.0;
    }

    auto normalized = std::fmod(azimuthDeg, DomainConstraints::maxAzimuthDeg);
    if (normalized < DomainConstraints::minAzimuthDeg) {
        normalized += DomainConstraints::maxAzimuthDeg;
    }
    if (normalized >= DomainConstraints::maxAzimuthDeg) {
        normalized -= DomainConstraints::maxAzimuthDeg;
    }
    return normalized;
}

bool AntennaMotionPlanner::isInBlindZone(double azimuthDeg) noexcept
{
    const auto normalized = normalizeAzimuth(azimuthDeg);
    return normalized > blindZoneStartDeg && normalized < blindZoneEndDeg;
}

double AntennaMotionPlanner::clampToSafeAngle(double azimuthDeg) noexcept
{
    const auto normalized = normalizeAzimuth(azimuthDeg);
    if (!isInBlindZone(normalized)) {
        return normalized;
    }
    return normalized < 180.0 ? blindZoneStartDeg : blindZoneEndDeg;
}

double AntennaMotionPlanner::toSafeCoord(double azimuthDeg) noexcept
{
    const auto safeAzimuth = clampToSafeAngle(azimuthDeg);
    if (safeAzimuth >= blindZoneEndDeg) {
        return safeAzimuth - blindZoneEndDeg;
    }
    return safeAzimuth + blindZoneStartDeg;
}

double AntennaMotionPlanner::fromSafeCoord(double safeCoordDeg) noexcept
{
    const auto clamped = std::clamp(safeCoordDeg, 0.0, maxSafeCoordDeg);
    if (clamped <= blindZoneStartDeg) {
        return normalizeAzimuth(blindZoneEndDeg + clamped);
    }
    return clamped - blindZoneStartDeg;
}

} // namespace siriusscope::core
