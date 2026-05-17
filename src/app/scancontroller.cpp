#include "scancontroller.h"

#include "bearingframebus.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <QMetaObject>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>
#include <utility>

namespace siriusscope::app {
namespace {

constexpr double kMinScanSpeedDegPerSec = 1.0;
constexpr double kMaxScanSpeedDegPerSec = 60.0;

bool isActiveState(ScanController::ScanState state)
{
    return state == ScanController::ScanState::MovingToStart
        || state == ScanController::ScanState::Scanning
        || state == ScanController::ScanState::Completing;
}

double angularDistanceDeg(double fromDeg, double toDeg)
{
    const auto from = core::AntennaMotionPlanner::normalizeAzimuth(fromDeg);
    const auto to = core::AntennaMotionPlanner::normalizeAzimuth(toDeg);
    const auto diff = std::abs(to - from);
    return std::min(diff, core::DomainConstraints::maxAzimuthDeg - diff);
}

std::string formatAngle(double value)
{
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << value;
    return stream.str();
}

} // namespace

ScanController::ScanController(hardware::IAntennaControl* antennaControl,
                               hardware::IAntennaAzimuthSource* azimuthSource,
                               BearingFrameBus* bearingFrameBus,
                               infrastructure::IDiagnosticsSink* diagnosticsSink,
                               QObject* parent)
    : QObject(parent)
    , m_antennaControl(antennaControl)
    , m_azimuthSource(azimuthSource)
    , m_bearingFrameBus(bearingFrameBus)
    , m_diagnosticsSink(diagnosticsSink)
{
    if (m_bearingFrameBus) {
        m_bearingFrameSubscriptionId = m_bearingFrameBus->subscribe(
            [this](std::vector<processing::BearingInputFrame> frames) mutable {
                QMetaObject::invokeMethod(this,
                                          [this, frames = std::move(frames)]() mutable {
                                              onBearingFrames(std::move(frames));
                                          },
                                          Qt::QueuedConnection);
            });
    }

    startAzimuthSource();
}

ScanController::~ScanController()
{
    if (m_bearingFrameBus && m_bearingFrameSubscriptionId != 0) {
        m_bearingFrameBus->unsubscribe(m_bearingFrameSubscriptionId);
    }
    if (m_azimuthSource) {
        m_azimuthSource->stop();
    }
}

double ScanController::selectedLeftAngle() const noexcept
{
    return m_selectedSector ? m_selectedSector->startAzimuthDeg : 0.0;
}

double ScanController::selectedRightAngle() const noexcept
{
    return m_selectedSector ? m_selectedSector->endAzimuthDeg : 0.0;
}

bool ScanController::scanActive() const noexcept
{
    return isActiveState(m_state);
}

QString ScanController::scanStateText() const
{
    switch (m_state) {
    case ScanState::Idle:
        return QStringLiteral("idle");
    case ScanState::SectorSelected:
        return QStringLiteral("sector selected");
    case ScanState::MovingToStart:
        return QStringLiteral("moving to start");
    case ScanState::Scanning:
        return QStringLiteral("scanning");
    case ScanState::Completing:
        return QStringLiteral("completing");
    case ScanState::Completed:
        return QStringLiteral("completed");
    case ScanState::Cancelled:
        return QStringLiteral("cancelled");
    case ScanState::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

void ScanController::selectSector(double leftAngleDeg, double rightAngleDeg)
{
    if (scanActive()) {
        setLastError(QStringLiteral("cannot change scan sector while scan is active"));
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: sector change rejected while scan is active");
        return;
    }

    const auto planned =
        core::AntennaMotionPlanner::planSectorScan(leftAngleDeg,
                                                   rightAngleDeg,
                                                   scanOptions(m_antennaSpeedDegPerSec));
    if (!planned) {
        const auto message = validationMessage(planned.validation());
        setLastError(message);
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: sector rejected: " + message.toStdString());
        return;
    }

    storeSelectedSector(planned.value()->requestedSector);
    setLastError({});
    setState(ScanState::SectorSelected);

    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: sector selected "
                + formatAngle(planned.value()->requestedSector.startAzimuthDeg)
                + ".."
                + formatAngle(planned.value()->requestedSector.endAzimuthDeg));
}

void ScanController::clearSector()
{
    if (scanActive()) {
        setLastError(QStringLiteral("cannot clear scan sector while scan is active"));
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: sector clear rejected while scan is active");
        return;
    }

    if (!m_selectedSector) {
        return;
    }

    m_selectedSector.reset();
    setProgress(0.0);
    setLastError({});
    setState(ScanState::Idle);
    emit selectedSectorChanged();
}

void ScanController::startSelectedSectorScan()
{
    startSelectedSectorScan(m_antennaSpeedDegPerSec);
}

void ScanController::startSelectedSectorScan(double speedDegPerSec)
{
    if (!m_selectedSector) {
        setLastError(QStringLiteral("scan sector is not selected"));
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: scan rejected: sector is not selected");
        return;
    }

    startScan(m_selectedSector->startAzimuthDeg,
              m_selectedSector->endAzimuthDeg,
              speedDegPerSec);
}

void ScanController::startScan(double leftAngleDeg, double rightAngleDeg)
{
    startScan(leftAngleDeg, rightAngleDeg, m_antennaSpeedDegPerSec);
}

void ScanController::startScan(double leftAngleDeg, double rightAngleDeg, double speedDegPerSec)
{
    if (scanActive()) {
        setLastError(QStringLiteral("scan is already active"));
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: scan rejected: already active");
        return;
    }

    QString speedError;
    if (!validateSpeed(speedDegPerSec, &speedError)) {
        setLastError(speedError);
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: scan rejected: " + speedError.toStdString());
        return;
    }
    if (std::abs(m_antennaSpeedDegPerSec - speedDegPerSec) > 0.001) {
        storeAntennaSpeed(speedDegPerSec);
    }

    const auto planned =
        core::AntennaMotionPlanner::planSectorScanFromCurrentAzimuth(leftAngleDeg,
                                                                     rightAngleDeg,
                                                                     m_currentAzimuthDeg,
                                                                     scanOptions(speedDegPerSec));
    if (!planned) {
        const auto message = validationMessage(planned.validation());
        setLastError(message);
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: sector rejected: " + message.toStdString());
        return;
    }

    if (!m_antennaControl) {
        failScan(QStringLiteral("antenna control is not configured"));
        return;
    }

    storeSelectedSector(planned.value()->requestedSector);

    ScanSession session;
    session.id = m_nextSessionId++;
    session.requestedSector = planned.value()->requestedSector;
    session.plannedPath = *planned.value();
    session.startedAt = std::chrono::system_clock::now();
    session.startAzimuthDeg = m_currentAzimuthDeg;
    session.lastAzimuthDeg = m_currentAzimuthDeg;
    session.speedDegPerSec = speedDegPerSec;
    m_activeSession = std::move(session);

    setProgress(0.0);
    setLastError({});
    setState(ScanState::MovingToStart);

    const auto moveResult = m_antennaControl->moveToAzimuth(m_activeSession->plannedPath.startAzimuthDeg);
    if (!moveResult) {
        failScan(QString::fromStdString("antenna command failed: " + moveResult.message));
        return;
    }

    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: scan started, moving to sector start");

    if (angularDistanceDeg(m_currentAzimuthDeg, m_activeSession->plannedPath.startAzimuthDeg)
        <= scanOptions(speedDegPerSec).angleToleranceDeg) {
        beginSectorScan();
    }
}

void ScanController::stopScan()
{
    if (!scanActive()) {
        if (m_antennaControl) {
            m_antennaControl->stop();
        }
        publish(infrastructure::DiagnosticSeverity::Info,
                "ScanController: antenna stop requested");
        return;
    }

    const auto sessionId = m_activeSession ? m_activeSession->id : 0;
    if (m_antennaControl) {
        m_antennaControl->stop();
    }

    m_activeSession.reset();
    setProgress(0.0);
    setState(ScanState::Cancelled);
    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: scan cancelled");
    emit scanCancelled(static_cast<qulonglong>(sessionId));
}

void ScanController::driveLeft(double speedDegPerSec)
{
    startManualMove(hardware::AntennaManualMoveCommand::Direction::Left, speedDegPerSec);
}

void ScanController::driveLeft()
{
    driveLeft(m_antennaSpeedDegPerSec);
}

void ScanController::driveRight(double speedDegPerSec)
{
    startManualMove(hardware::AntennaManualMoveCommand::Direction::Right, speedDegPerSec);
}

void ScanController::driveRight()
{
    driveRight(m_antennaSpeedDegPerSec);
}

void ScanController::setAntennaSpeedDegPerSec(double speedDegPerSec)
{
    if (scanActive()) {
        setLastError(QStringLiteral("cannot change antenna speed while scan is active"));
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: antenna speed change rejected while scan is active");
        return;
    }

    QString speedError;
    if (!validateSpeed(speedDegPerSec, &speedError)) {
        setLastError(speedError);
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: antenna speed rejected: " + speedError.toStdString());
        return;
    }

    if (std::abs(m_antennaSpeedDegPerSec - speedDegPerSec) <= 0.001) {
        return;
    }

    storeAntennaSpeed(speedDegPerSec);
    setLastError({});
}

void ScanController::setScanSpeedDegPerSec(double speedDegPerSec)
{
    setAntennaSpeedDegPerSec(speedDegPerSec);
}

void ScanController::startAzimuthSource()
{
    if (!m_azimuthSource) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: azimuth source is not configured");
        return;
    }

    const auto result = m_azimuthSource->start(
        [this](const hardware::AntennaAzimuthSample& sample) {
            handleAzimuthSample(sample);
        });
    if (!result) {
        setLastError(QString::fromStdString(result.message));
        publish(infrastructure::DiagnosticSeverity::Error,
                "ScanController: azimuth source start failed: " + result.message);
    }
}

void ScanController::handleAzimuthSample(const hardware::AntennaAzimuthSample& sample)
{
    QMetaObject::invokeMethod(this,
                              [this, sample] {
                                  updateAzimuth(sample);
                              },
                              Qt::QueuedConnection);
}

void ScanController::updateAzimuth(const hardware::AntennaAzimuthSample& sample)
{
    const auto normalized = core::AntennaMotionPlanner::normalizeAzimuth(sample.degrees);
    const bool changed = std::abs(m_currentAzimuthDeg - normalized) > 0.001;
    m_currentAzimuthDeg = normalized;
    m_lastAzimuthTimestamp = sample.timestamp;

    if (m_activeSession) {
        m_activeSession->lastAzimuthDeg = normalized;
    }

    if (changed) {
        emit currentAzimuthChanged();
    }

    if (!m_activeSession) {
        return;
    }

    if (m_state == ScanState::MovingToStart
        && angularDistanceDeg(normalized, m_activeSession->plannedPath.startAzimuthDeg)
            <= scanOptions(m_activeSession->speedDegPerSec).angleToleranceDeg) {
        beginSectorScan();
        return;
    }

    if (m_state != ScanState::Scanning) {
        return;
    }

    const auto safeCoord = core::AntennaMotionPlanner::toSafeCoord(normalized);
    const auto& path = m_activeSession->plannedPath;
    const auto traveled =
        path.direction == core::ScanDirection::IncreasingSafeCoord
        ? safeCoord - path.safeStartCoordDeg
        : path.safeStartCoordDeg - safeCoord;
    const auto progress = std::clamp(traveled / path.spanDeg, 0.0, 1.0);
    m_activeSession->progress = progress;
    setProgress(progress);

    if (progress >= 1.0
        || angularDistanceDeg(normalized, path.endAzimuthDeg)
            <= scanOptions(m_activeSession->speedDegPerSec).angleToleranceDeg) {
        completeScan();
    }
}

void ScanController::beginSectorScan()
{
    if (!m_activeSession || m_state != ScanState::MovingToStart || !m_antennaControl) {
        return;
    }

    const auto& path = m_activeSession->plannedPath;
    hardware::AntennaSectorScanCommand command;
    command.requestedSector = path.requestedSector;
    command.startAzimuthDeg = path.startAzimuthDeg;
    command.endAzimuthDeg = path.endAzimuthDeg;
    command.safeStartCoordDeg = path.safeStartCoordDeg;
    command.safeEndCoordDeg = path.safeEndCoordDeg;
    command.speedDegPerSec = m_activeSession->speedDegPerSec;
    command.direction = path.direction;

    const auto result = m_antennaControl->startSectorScan(command);
    if (!result) {
        failScan(QString::fromStdString("antenna command failed: " + result.message));
        return;
    }

    setProgress(0.0);
    setState(ScanState::Scanning);
    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: sector scan command accepted");
}

void ScanController::completeScan()
{
    if (!m_activeSession) {
        return;
    }

    setState(ScanState::Completing);
    const auto sessionId = m_activeSession->id;
    const int frameCount =
        static_cast<int>(m_activeSession->collectedBearingFrames.size());
    m_activeSession->finishedAt = std::chrono::system_clock::now();

    if (m_antennaControl) {
        m_antennaControl->stop();
    }

    m_activeSession.reset();
    setProgress(1.0);
    setState(ScanState::Completed);

    if (frameCount == 0) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: no bearing frames collected during scan");
    }

    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: scan completed, frames=" + std::to_string(frameCount));
    emit scanCompleted(static_cast<qulonglong>(sessionId), frameCount);
}

void ScanController::failScan(const QString& reason)
{
    const auto sessionId = m_activeSession ? m_activeSession->id : 0;
    if (m_antennaControl && scanActive()) {
        m_antennaControl->stop();
    }

    m_activeSession.reset();
    setLastError(reason);
    setProgress(0.0);
    setState(ScanState::Failed);
    publish(infrastructure::DiagnosticSeverity::Error,
            "ScanController: " + reason.toStdString());
    emit scanFailed(static_cast<qulonglong>(sessionId), reason);
}

void ScanController::onBearingFrames(std::vector<processing::BearingInputFrame> frames)
{
    if (!m_activeSession || m_state != ScanState::Scanning || frames.empty()) {
        return;
    }

    m_activeSession->collectedBearingFrames.insert(
        m_activeSession->collectedBearingFrames.end(),
        std::make_move_iterator(frames.begin()),
        std::make_move_iterator(frames.end()));
}

void ScanController::startManualMove(hardware::AntennaManualMoveCommand::Direction direction,
                                     double speedDegPerSec)
{
    if (scanActive()) {
        setLastError(QStringLiteral("manual antenna move is disabled during scan"));
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: manual antenna move rejected while scan is active");
        return;
    }

    QString speedError;
    if (!validateSpeed(speedDegPerSec, &speedError)) {
        setLastError(speedError);
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: manual antenna move rejected: " + speedError.toStdString());
        return;
    }

    if (!m_antennaControl) {
        setLastError(QStringLiteral("antenna control is not configured"));
        publish(infrastructure::DiagnosticSeverity::Error,
                "ScanController: antenna control is not configured");
        return;
    }

    const auto result = m_antennaControl->startManualMove(
        hardware::AntennaManualMoveCommand{direction, speedDegPerSec});
    if (!result) {
        setLastError(QString::fromStdString("antenna command failed: " + result.message));
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: antenna command failed: " + result.message);
        return;
    }

    setLastError({});
    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: manual antenna move started");
}

void ScanController::setState(ScanState state)
{
    if (m_state == state) {
        return;
    }

    m_state = state;
    emit scanStateChanged();
}

void ScanController::setProgress(double progress)
{
    const auto clamped = std::clamp(progress, 0.0, 1.0);
    if (std::abs(m_scanProgress - clamped) <= 0.001) {
        return;
    }

    m_scanProgress = clamped;
    emit scanProgressChanged();
}

void ScanController::setLastError(QString error)
{
    if (m_lastError == error) {
        return;
    }

    m_lastError = std::move(error);
    emit scanStateChanged();
}

void ScanController::storeSelectedSector(const core::ScanSector& sector)
{
    const bool changed =
        !m_selectedSector
        || std::abs(m_selectedSector->startAzimuthDeg - sector.startAzimuthDeg) > 0.001
        || std::abs(m_selectedSector->endAzimuthDeg - sector.endAzimuthDeg) > 0.001;
    m_selectedSector = sector;
    if (changed) {
        emit selectedSectorChanged();
    }
}

core::ScanMotionOptions ScanController::scanOptions(double speedDegPerSec) const
{
    core::ScanMotionOptions options;
    options.speedDegPerSec = speedDegPerSec;
    return options;
}

QString ScanController::validationMessage(const core::ValidationResult& validation) const
{
    QStringList parts;
    for (const auto& issue : validation.issues()) {
        parts.push_back(QString::fromStdString(issue.message.empty()
                                                   ? "validation failed"
                                                   : issue.message));
    }
    return parts.isEmpty() ? QStringLiteral("validation failed") : parts.join(QStringLiteral("; "));
}

bool ScanController::validateSpeed(double speedDegPerSec, QString* error) const
{
    if (!std::isfinite(speedDegPerSec)) {
        if (error) {
            *error = QStringLiteral("antenna speed must be finite");
        }
        return false;
    }

    if (speedDegPerSec < kMinScanSpeedDegPerSec
        || speedDegPerSec > kMaxScanSpeedDegPerSec) {
        if (error) {
            *error = QStringLiteral("antenna speed must be in range 1..60 deg/s");
        }
        return false;
    }

    return true;
}

void ScanController::storeAntennaSpeed(double speedDegPerSec)
{
    m_antennaSpeedDegPerSec = speedDegPerSec;
    emit antennaSpeedChanged();
    emit scanSpeedChanged();
}

void ScanController::publish(infrastructure::DiagnosticSeverity severity,
                             const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "ScanController",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::app
