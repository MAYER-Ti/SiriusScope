#include "antennacontrollerstub.h"

#include "core/domain_models.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QString>

namespace {

constexpr double blindZoneLeftDeg = 170.0;
constexpr double blindZoneRightDeg = 190.0;
constexpr double maxAzimuthDeg = 360.0;
constexpr double maxSafeCoordDeg = 340.0;
constexpr double angleToleranceDeg = 0.2;
constexpr int motionIntervalMs = 100;

QString commandContext(const char *commandName)
{
    return QString::fromLatin1(commandName);
}

} // namespace

AntennaControllerStub::AntennaControllerStub(QObject *parent)
    : QObject(parent)
{
    m_motionTimer.setInterval(motionIntervalMs);
    m_motionTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_motionTimer, &QTimer::timeout, this, &AntennaControllerStub::advanceMotion);
}

double AntennaControllerStub::azimuthDeg() const noexcept
{
    return m_azimuthDeg;
}

void AntennaControllerStub::setAzimuthDeg(double azimuthDeg)
{
    if (!validateAzimuth(azimuthDeg, "azimuthDeg", "setAzimuthDeg")) {
        return;
    }

    const auto normalized = normalizeAzimuth(azimuthDeg);
    if (qFuzzyCompare(m_azimuthDeg, normalized)) {
        return;
    }

    m_azimuthDeg = normalized;
    emit azimuthDegChanged(m_azimuthDeg);
}

void AntennaControllerStub::stop()
{
    stopMotion();
    qInfo().noquote() << QStringLiteral("antenna stop");
    emit stopped();
}

void AntennaControllerStub::driveLeft(int speed)
{
    if (!validateSpeed(speed, "driveLeft")) {
        return;
    }

    qInfo().noquote() << QStringLiteral("antenna driveLeft speed=%1").arg(speed);
    startManualMotion(MotionMode::DriveLeft, speed);
    emit driveLeftCommanded(speed);
}

void AntennaControllerStub::driveRight(int speed)
{
    if (!validateSpeed(speed, "driveRight")) {
        return;
    }

    qInfo().noquote() << QStringLiteral("antenna driveRight speed=%1").arg(speed);
    startManualMotion(MotionMode::DriveRight, speed);
    emit driveRightCommanded(speed);
}

void AntennaControllerStub::scan(double leftAngle, double rightAngle, int speed)
{
    if (!validateSafeScanAngle(leftAngle, "leftAngle", "scan")
        || !validateSafeScanAngle(rightAngle, "rightAngle", "scan")
        || !validateSpeed(speed, "scan")) {
        return;
    }

    const auto leftCoord = toSafeCoord(leftAngle);
    const auto rightCoord = toSafeCoord(rightAngle);
    const auto minCoord = std::min(leftCoord, rightCoord);
    const auto maxCoord = std::max(leftCoord, rightCoord);
    if (std::abs(maxCoord - minCoord) <= angleToleranceDeg) {
        const auto reason = QStringLiteral("scan rejected: sector width must be positive");
        qWarning().noquote() << reason;
        emit commandRejected(reason);
        return;
    }

    if (isInBlindZone(m_azimuthDeg)) {
        const auto reason = QStringLiteral("scan rejected: current antenna angle is inside blind zone");
        qWarning().noquote() << reason;
        emit commandRejected(reason);
        return;
    }

    qInfo().noquote()
        << QStringLiteral("antenna scan left=%1 right=%2 speed=%3")
               .arg(leftAngle, 0, 'f', 1)
               .arg(rightAngle, 0, 'f', 1)
               .arg(speed);
    startScanMotion(minCoord, maxCoord, speed);
    emit scanCommanded(leftAngle, rightAngle, speed);
}

bool AntennaControllerStub::validateSpeed(int speed, const char *commandName)
{
    if (speed <= 0) {
        const auto reason = QStringLiteral("%1 rejected: speed must be positive")
                                .arg(commandContext(commandName));
        qWarning().noquote() << reason;
        emit commandRejected(reason);
        return false;
    }

    return true;
}

bool AntennaControllerStub::validateSafeScanAngle(double azimuthDeg,
                                                  const char *fieldName,
                                                  const char *commandName)
{
    if (!validateAzimuth(azimuthDeg, fieldName, commandName)) {
        return false;
    }

    if (isInBlindZone(azimuthDeg)) {
        const auto reason = QStringLiteral("%1 rejected: %2 is inside antenna blind zone")
                                .arg(commandContext(commandName), QString::fromLatin1(fieldName));
        qWarning().noquote() << reason;
        emit commandRejected(reason);
        return false;
    }

    return true;
}

bool AntennaControllerStub::validateAzimuth(double azimuthDeg,
                                            const char *fieldName,
                                            const char *commandName)
{
    const auto validation = siriusscope::core::validateAzimuth(azimuthDeg);
    if (!validation) {
        const auto reason = QStringLiteral("%1 rejected: %2 must be in range [0, 360)")
                                .arg(commandContext(commandName), QString::fromLatin1(fieldName));
        qWarning().noquote() << reason;
        emit commandRejected(reason);
        return false;
    }

    return true;
}

void AntennaControllerStub::startManualMotion(MotionMode mode, int speed)
{
    m_motionMode = mode;
    m_speedDegPerSec = static_cast<double>(speed);
    m_motionClock.restart();
    m_motionTimer.start();
}

void AntennaControllerStub::startScanMotion(double leftCoord, double rightCoord, int speed)
{
    const auto currentCoord = toSafeCoord(m_azimuthDeg);
    const auto leftDistance = safeCoordDistance(currentCoord, leftCoord);
    const auto rightDistance = safeCoordDistance(currentCoord, rightCoord);

    if (leftDistance <= rightDistance) {
        m_scanStartCoord = leftCoord;
        m_scanTargetCoord = rightCoord;
    } else {
        m_scanStartCoord = rightCoord;
        m_scanTargetCoord = leftCoord;
    }

    if (safeCoordDistance(currentCoord, m_scanStartCoord) <= angleToleranceDeg) {
        m_motionMode = MotionMode::ScanSector;
    } else {
        m_motionMode = MotionMode::MoveToScanStart;
    }

    m_speedDegPerSec = static_cast<double>(speed);
    m_motionClock.restart();
    m_motionTimer.start();
}

void AntennaControllerStub::stopMotion()
{
    m_motionTimer.stop();
    m_motionMode = MotionMode::Idle;
    m_speedDegPerSec = 0.0;
}

void AntennaControllerStub::advanceMotion()
{
    if (m_motionMode == MotionMode::Idle) {
        m_motionTimer.stop();
        return;
    }

    const auto elapsedMs = m_motionClock.restart();
    const auto elapsedSec = static_cast<double>(std::max<qint64>(elapsedMs, 1)) / 1000.0;

    switch (m_motionMode) {
    case MotionMode::DriveLeft:
        advanceDriveLeft(elapsedSec);
        break;
    case MotionMode::DriveRight:
        advanceDriveRight(elapsedSec);
        break;
    case MotionMode::MoveToScanStart:
        advanceMoveToScanStart(elapsedSec);
        break;
    case MotionMode::ScanSector:
        advanceScanSector(elapsedSec);
        break;
    case MotionMode::Idle:
        break;
    }
}

void AntennaControllerStub::advanceDriveLeft(double elapsedSec)
{
    const auto current = normalizeAzimuth(m_azimuthDeg);
    if (isInBlindZone(current)) {
        stopMotion();
        emit stopped();
        return;
    }

    const auto delta = m_speedDegPerSec * elapsedSec;
    if (current >= blindZoneRightDeg) {
        const auto next = current - delta;
        if (next <= blindZoneRightDeg) {
            setAzimuthDeg(blindZoneRightDeg);
            stopMotion();
            emit stopped();
            return;
        }

        setAzimuthDeg(next);
        return;
    }

    const auto distanceToRightBoundary = current + (maxAzimuthDeg - blindZoneRightDeg);
    if (delta >= distanceToRightBoundary) {
        setAzimuthDeg(blindZoneRightDeg);
        stopMotion();
        emit stopped();
        return;
    }

    setAzimuthDeg(normalizeAzimuth(current - delta));
}

void AntennaControllerStub::advanceDriveRight(double elapsedSec)
{
    const auto current = normalizeAzimuth(m_azimuthDeg);
    if (isInBlindZone(current)) {
        stopMotion();
        emit stopped();
        return;
    }

    const auto delta = m_speedDegPerSec * elapsedSec;
    if (current <= blindZoneLeftDeg) {
        const auto next = current + delta;
        if (next >= blindZoneLeftDeg) {
            setAzimuthDeg(blindZoneLeftDeg);
            stopMotion();
            emit stopped();
            return;
        }

        setAzimuthDeg(next);
        return;
    }

    const auto distanceToLeftBoundary = maxAzimuthDeg - current + blindZoneLeftDeg;
    if (delta >= distanceToLeftBoundary) {
        setAzimuthDeg(blindZoneLeftDeg);
        stopMotion();
        emit stopped();
        return;
    }

    setAzimuthDeg(normalizeAzimuth(current + delta));
}

void AntennaControllerStub::advanceMoveToScanStart(double elapsedSec)
{
    if (moveTowardSafeCoord(m_scanStartCoord, elapsedSec)) {
        m_motionMode = MotionMode::ScanSector;
    }
}

void AntennaControllerStub::advanceScanSector(double elapsedSec)
{
    if (moveTowardSafeCoord(m_scanTargetCoord, elapsedSec)) {
        stopMotion();
        emit stopped();
    }
}

bool AntennaControllerStub::moveTowardSafeCoord(double targetCoord, double elapsedSec)
{
    const auto currentCoord = toSafeCoord(m_azimuthDeg);
    const auto distance = targetCoord - currentCoord;
    if (std::abs(distance) <= angleToleranceDeg) {
        setAzimuthDeg(fromSafeCoord(targetCoord));
        return true;
    }

    const auto maxStep = m_speedDegPerSec * elapsedSec;
    const auto step = std::clamp(distance, -maxStep, maxStep);
    const auto nextCoord = currentCoord + step;
    if (std::abs(targetCoord - nextCoord) <= angleToleranceDeg) {
        setAzimuthDeg(fromSafeCoord(targetCoord));
        return true;
    }

    setAzimuthDeg(fromSafeCoord(nextCoord));
    return false;
}

double AntennaControllerStub::normalizeAzimuth(double azimuthDeg) const noexcept
{
    auto result = std::fmod(azimuthDeg, maxAzimuthDeg);
    if (result < 0.0) {
        result += maxAzimuthDeg;
    }

    if (qFuzzyCompare(result, maxAzimuthDeg)) {
        return 0.0;
    }

    return result;
}

bool AntennaControllerStub::isInBlindZone(double azimuthDeg) const noexcept
{
    const auto normalized = normalizeAzimuth(azimuthDeg);
    return normalized > blindZoneLeftDeg && normalized < blindZoneRightDeg;
}

double AntennaControllerStub::toSafeCoord(double azimuthDeg) const noexcept
{
    const auto normalized = normalizeAzimuth(azimuthDeg);
    if (normalized >= blindZoneRightDeg) {
        return normalized - blindZoneRightDeg;
    }

    return normalized + blindZoneLeftDeg;
}

double AntennaControllerStub::fromSafeCoord(double safeCoord) const noexcept
{
    const auto clamped = std::clamp(safeCoord, 0.0, maxSafeCoordDeg);
    if (clamped <= blindZoneLeftDeg) {
        return normalizeAzimuth(blindZoneRightDeg + clamped);
    }

    return clamped - blindZoneLeftDeg;
}

double AntennaControllerStub::safeCoordDistance(double fromCoord, double toCoord) const noexcept
{
    return std::abs(toCoord - fromCoord);
}
