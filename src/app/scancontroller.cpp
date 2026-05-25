#include "scancontroller.h"

#include "bearingframebus.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "signalsamplebus.h"

#include <QMetaObject>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
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

std::int64_t toUtcNs(std::chrono::system_clock::time_point timePoint)
{
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint.time_since_epoch())
            .count();
    return ns < 0 ? 0 : static_cast<std::int64_t>(ns);
}

std::uint64_t minSampleIndex(const std::vector<processing::BearingInputFrame>& frames)
{
    auto minIndex = std::numeric_limits<std::uint64_t>::max();
    for (const auto& frame : frames) {
        minIndex = std::min(minIndex, frame.sampleIndexStart);
        for (const auto& candidate : frame.candidates) {
            minIndex = std::min(minIndex, candidate.sampleIndexStart);
        }
    }

    return minIndex == std::numeric_limits<std::uint64_t>::max() ? 0 : minIndex;
}

infrastructure::DiagnosticSeverity mapSeverity(
    processing::ProcessingDiagnosticSeverity severity)
{
    switch (severity) {
    case processing::ProcessingDiagnosticSeverity::Info:
        return infrastructure::DiagnosticSeverity::Info;
    case processing::ProcessingDiagnosticSeverity::Warning:
        return infrastructure::DiagnosticSeverity::Warning;
    case processing::ProcessingDiagnosticSeverity::Error:
        return infrastructure::DiagnosticSeverity::Error;
    }

    return infrastructure::DiagnosticSeverity::Warning;
}

QString frequencyMHzText(const std::vector<std::int64_t>& frequenciesHz)
{
    QStringList parts;
    for (const auto frequencyHz : frequenciesHz) {
        parts.push_back(QString::number(static_cast<double>(frequencyHz) / 1'000'000.0,
                                        'f',
                                        1));
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

ScanController::ScanController(hardware::IAntennaControl* antennaControl,
                               hardware::IAntennaAzimuthSource* azimuthSource,
                               BearingFrameBus* bearingFrameBus,
                               SignalSampleBus* signalSampleBus,
                               processing::BearingService* bearingService,
                               IScanAcquisitionRecorder* scanAcquisitionRecorder,
                               IProcessingFlushControl* processingFlushControl,
                               IScanRecordingControl* scanRecordingControl,
                               IResultTableSink* resultTableSink,
                               infrastructure::IDiagnosticsSink* diagnosticsSink,
                               QObject* parent)
    : QObject(parent)
    , m_antennaControl(antennaControl)
    , m_azimuthSource(azimuthSource)
    , m_bearingFrameBus(bearingFrameBus)
    , m_signalSampleBus(signalSampleBus)
    , m_bearingService(bearingService)
    , m_scanAcquisitionRecorder(scanAcquisitionRecorder)
    , m_processingFlushControl(processingFlushControl)
    , m_scanRecordingControl(scanRecordingControl)
    , m_resultTableSink(resultTableSink)
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

    if (m_signalSampleBus) {
        m_signalSampleSubscriptionId = m_signalSampleBus->subscribe(
            [this](std::vector<core::SignalSample> samples) mutable {
                QMetaObject::invokeMethod(this,
                                          [this, samples = std::move(samples)]() mutable {
                                              onSignalSamples(std::move(samples));
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
    if (m_signalSampleBus && m_signalSampleSubscriptionId != 0) {
        m_signalSampleBus->unsubscribe(m_signalSampleSubscriptionId);
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
    if (!m_scanAcquisitionRecorder) {
        failScan(QStringLiteral("scan acquisition recorder is not configured"));
        return;
    }

    storeSelectedSector(planned.value()->requestedSector);
    clearBearingResults();
    m_signalParameterAccumulator.reset();
    m_collectedSignalSampleCount = 0;
    m_lastSignalParameters.clear();

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

    closeActiveAcquisitionWithoutCalculation(sessionId);
    endScanRecording(sessionId);

    m_signalParameterAccumulator.reset();
    m_collectedSignalSampleCount = 0;
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
    if (sample.timestamp != std::chrono::system_clock::time_point{}) {
        const auto now = std::chrono::system_clock::now();
        const auto latencyMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - sample.timestamp).count();
        const auto steadyNow = std::chrono::steady_clock::now();
        if (latencyMs > 150 && steadyNow >= m_nextAzimuthLatencyDiagnostic) {
            publish(infrastructure::DiagnosticSeverity::Warning,
                    "ScanController: azimuth UI latency " + std::to_string(latencyMs) + " ms");
            m_nextAzimuthLatencyDiagnostic = steadyNow + std::chrono::seconds{1};
        }
    }

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

    const auto sessionId = m_activeSession->id;
    if (m_scanRecordingControl) {
        const auto recordingResult = m_scanRecordingControl->beginScanRecording(sessionId);
        if (!recordingResult) {
            publish(infrastructure::DiagnosticSeverity::Warning,
                    "ScanController: scan recording start failed: " + recordingResult.message);
        }
    }

    ScanAcquisitionMetadata metadata;
    metadata.scanSessionId = sessionId;
    metadata.requestedSector = m_activeSession->requestedSector;
    metadata.startedAt = std::chrono::system_clock::now();
    metadata.finishedAt = metadata.startedAt;
    metadata.startAzimuthDeg = m_currentAzimuthDeg;
    metadata.endAzimuthDeg = m_currentAzimuthDeg;
    metadata.speedDegPerSec = m_activeSession->speedDegPerSec;

    const auto beginResult = m_scanAcquisitionRecorder->begin(metadata);
    if (!beginResult) {
        failScan(QString::fromStdString(
            "scan acquisition start failed: " + beginResult.message));
        return;
    }
    m_activeSession->acquisitionMetadata = metadata;
    m_activeSession->timeBase.reset();

    setProgress(0.0);
    setState(ScanState::Scanning);
    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: scan acquisition session opened");
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
    m_activeSession->finishedAt = std::chrono::system_clock::now();

    if (m_antennaControl) {
        m_antennaControl->stop();
    }

    if (m_processingFlushControl) {
        m_processingFlushControl->flushProcessingAsync(
            std::chrono::milliseconds{1500},
            [this, sessionId](core::OperationResult flushResult) {
                QMetaObject::invokeMethod(this,
                                          [this, sessionId, flushResult = std::move(flushResult)] {
                                              if (!flushResult) {
                                                  publish(
                                                      infrastructure::DiagnosticSeverity::Warning,
                                                      "ScanController: processing flush before "
                                                      "bearing calculation failed: "
                                                          + flushResult.message);
                                              }
                                              finalizeCompletedScan(sessionId);
                                          },
                                          Qt::QueuedConnection);
            });
        return;
    }

    QMetaObject::invokeMethod(this,
                              [this, sessionId] {
                                  finalizeCompletedScan(sessionId);
                              },
                              Qt::QueuedConnection);
}

void ScanController::finalizeCompletedScan(std::uint64_t sessionId)
{
    if (!m_activeSession || m_activeSession->id != sessionId) {
        return;
    }

    const auto finishedAt = m_activeSession->finishedAt == std::chrono::system_clock::time_point{}
        ? std::chrono::system_clock::now()
        : m_activeSession->finishedAt;

    core::TimeBase timeBase;
    if (m_activeSession->timeBase) {
        timeBase = *m_activeSession->timeBase;
    } else if (auto created = core::TimeBase::create(
                   toUtcNs(m_activeSession->acquisitionMetadata.startedAt),
                   0,
                   core::DomainConstraints::defaultSamplePeriodNs)) {
        timeBase = *created.value();
        m_activeSession->timeBase = timeBase;
    }

    auto finalMetadata = m_activeSession->acquisitionMetadata;
    finalMetadata.finishedAt = finishedAt;
    finalMetadata.endAzimuthDeg = m_activeSession->lastAzimuthDeg;
    finalMetadata.timeBase = m_activeSession->timeBase;

    endScanRecording(sessionId);

    bool acquisitionClosed = false;
    if (m_scanAcquisitionRecorder && m_scanAcquisitionRecorder->active()) {
        const auto closeResult = m_scanAcquisitionRecorder->close(finalMetadata);
        if (!closeResult) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "ScanController: scan acquisition close failed: " + closeResult.message);
        } else {
            acquisitionClosed = true;
        }
    }

    auto observations = m_scanAcquisitionRecorder
        ? m_scanAcquisitionRecorder->observations(sessionId)
        : std::vector<processing::BearingFrameObservation>{};
    const int frameCount = static_cast<int>(observations.size());
    if (acquisitionClosed) {
        publish(infrastructure::DiagnosticSeverity::Info,
                "ScanController: scan acquisition session closed, observations="
                    + std::to_string(frameCount));
    }

    if (m_bearingService) {
        publish(infrastructure::DiagnosticSeverity::Info,
                "ScanController: bearing calculation started");
        const auto calculation = m_bearingService->calculate(
            observations,
            timeBase,
            core::defaultRuntimeCapabilities());
        m_lastBearingResults = calculation.results;
        rebuildBearingPresentation();
        publishProcessingDiagnostics(calculation.diagnostics);
        publish(infrastructure::DiagnosticSeverity::Info,
                "ScanController: bearing calculation completed, results="
                    + std::to_string(m_lastBearingResults.size()));
    } else {
        m_lastBearingResults.clear();
        rebuildBearingPresentation();
        publish(infrastructure::DiagnosticSeverity::Error,
                "ScanController: bearing service is not configured");
    }

    emit bearingResultsChanged();
    emit bearingResultsReady(static_cast<qulonglong>(sessionId),
                             static_cast<int>(m_lastBearingResults.size()));
    emit bearingResultsCalculated(static_cast<qulonglong>(sessionId), m_targetBearings);

    m_lastSignalParameters = m_signalParameterAccumulator.finalize();
    m_collectedSignalSampleCount = m_signalParameterAccumulator.acceptedSampleCount();
    if (m_lastSignalParameters.empty()) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: no signal parameters calculated by streaming accumulator, "
                "acceptedSamples="
                    + std::to_string(m_collectedSignalSampleCount));
    } else {
        publish(infrastructure::DiagnosticSeverity::Info,
                "ScanController: signal parameters calculated, bands="
                    + std::to_string(m_lastSignalParameters.size())
                    + ", acceptedSamples=" + std::to_string(m_collectedSignalSampleCount)
                    + ", raw samples not retained");
    }
    emit signalParametersCalculated(static_cast<qulonglong>(sessionId),
                                    static_cast<int>(m_lastSignalParameters.size()));

    if (m_resultTableSink) {
        ResultTableAppendContext context;
        context.scanSessionId = sessionId;
        context.antennaAzimuthDeg = m_activeSession->lastAzimuthDeg;
        context.signalParameters = m_lastSignalParameters;
        const auto result = m_resultTableSink->appendBearingResults(context,
                                                                    m_lastBearingResults);
        if (!result) {
            publish(infrastructure::DiagnosticSeverity::Warning,
                    "ScanController: result table append failed: " + result.message);
        }
    }

    m_activeSession.reset();
    setProgress(1.0);
    setState(ScanState::Completed);

    if (frameCount == 0) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: scan acquisition has no observations");
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

    closeActiveAcquisitionWithoutCalculation(sessionId);
    endScanRecording(sessionId);

    m_signalParameterAccumulator.reset();
    m_collectedSignalSampleCount = 0;
    m_activeSession.reset();
    setLastError(reason);
    setProgress(0.0);
    setState(ScanState::Failed);
    publish(infrastructure::DiagnosticSeverity::Error,
            "ScanController: " + reason.toStdString());
    emit scanFailed(static_cast<qulonglong>(sessionId), reason);
}

void ScanController::closeActiveAcquisitionWithoutCalculation(std::uint64_t sessionId)
{
    if (!m_activeSession || !m_scanAcquisitionRecorder || !m_scanAcquisitionRecorder->active()) {
        return;
    }

    auto finalMetadata = m_activeSession->acquisitionMetadata;
    finalMetadata.finishedAt = std::chrono::system_clock::now();
    finalMetadata.endAzimuthDeg = m_activeSession->lastAzimuthDeg;
    finalMetadata.timeBase = m_activeSession->timeBase;

    const auto result = m_scanAcquisitionRecorder->close(finalMetadata);
    if (!result) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "ScanController: scan acquisition close failed: " + result.message);
        return;
    }

    const auto observationCount =
        m_scanAcquisitionRecorder->observations(sessionId).size();
    publish(infrastructure::DiagnosticSeverity::Info,
            "ScanController: scan acquisition session closed, observations="
                + std::to_string(observationCount));
}

void ScanController::endScanRecording(std::uint64_t sessionId)
{
    if (!m_scanRecordingControl) {
        return;
    }

    const auto result = m_scanRecordingControl->endScanRecording(sessionId);
    if (!result) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: scan recording stop failed: " + result.message);
    }
}

void ScanController::onBearingFrames(std::vector<processing::BearingInputFrame> frames)
{
    const bool acceptsFrames = m_state == ScanState::Scanning
        || (m_state == ScanState::Completing
            && m_scanAcquisitionRecorder
            && m_scanAcquisitionRecorder->active());
    if (!m_activeSession || !acceptsFrames || frames.empty()) {
        return;
    }
    if (!m_scanAcquisitionRecorder || !m_scanAcquisitionRecorder->active()) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "ScanController: bearing frame ignored because scan acquisition is not active");
        return;
    }

    if (!m_activeSession->timeBase) {
        auto created = core::TimeBase::create(
            toUtcNs(m_activeSession->acquisitionMetadata.startedAt),
            minSampleIndex(frames),
            core::DomainConstraints::defaultSamplePeriodNs);
        if (created) {
            m_activeSession->timeBase = *created.value();
            m_activeSession->acquisitionMetadata.timeBase = m_activeSession->timeBase;
        } else {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "ScanController: failed to create bearing timebase");
        }
    }

    const auto observedUtcNs = toUtcNs(std::chrono::system_clock::now());
    const auto azimuthDeg = m_activeSession->lastAzimuthDeg;
    for (auto& frame : frames) {
        processing::BearingFrameObservation observation{
            std::move(frame),
            azimuthDeg,
            observedUtcNs,
        };
        const auto appendResult = m_scanAcquisitionRecorder->append(observation);
        if (!appendResult) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "ScanController: failed to append scan acquisition observation: "
                        + appendResult.message);
        }
    }
}

void ScanController::onSignalSamples(std::vector<core::SignalSample> samples)
{
    if (!m_activeSession
        || !(m_state == ScanState::Scanning || m_state == ScanState::Completing)
        || samples.empty()) {
        return;
    }

    m_signalParameterAccumulator.ingest(samples);
    m_collectedSignalSampleCount = m_signalParameterAccumulator.acceptedSampleCount();
}

void ScanController::clearBearingResults()
{
    if (m_lastBearingResults.empty() && m_targetBearings.empty()
        && m_targetAzimuthsDeg.empty()) {
        return;
    }

    m_lastBearingResults.clear();
    m_targetBearings.clear();
    m_targetAzimuthsDeg.clear();
    emit bearingResultsChanged();
}

void ScanController::rebuildBearingPresentation()
{
    m_targetBearings.clear();
    m_targetAzimuthsDeg.clear();

    for (const auto& result : m_lastBearingResults) {
        m_targetAzimuthsDeg.push_back(result.bearingAzimuthDeg);

        QVariantList frequenciesHz;
        for (const auto frequencyHz : result.frequenciesHz) {
            frequenciesHz.push_back(
                QVariant::fromValue(static_cast<qlonglong>(frequencyHz)));
        }

        QVariantMap bearing;
        bearing.insert(QStringLiteral("azimuthDeg"), result.bearingAzimuthDeg);
        bearing.insert(QStringLiteral("bandIndex"), result.bandIndex);
        bearing.insert(QStringLiteral("quality"), result.quality.value_or(0.0));
        bearing.insert(QStringLiteral("frequencyMHz"), frequencyMHzText(result.frequenciesHz));
        bearing.insert(QStringLiteral("frequenciesHz"), frequenciesHz);
        bearing.insert(QStringLiteral("sampleIndex"),
                       QVariant::fromValue(static_cast<qulonglong>(result.sampleIndex)));
        bearing.insert(QStringLiteral("resultTimeUtcNs"),
                       QVariant::fromValue(static_cast<qlonglong>(result.resultTimeUtcNs)));
        m_targetBearings.push_back(std::move(bearing));
    }
}

void ScanController::publishProcessingDiagnostics(
    const std::vector<processing::ProcessingDiagnostic>& diagnostics) const
{
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == processing::ProcessingErrorCode::None
            && diagnostic.message.empty()) {
            continue;
        }

        publish(mapSeverity(diagnostic.severity),
                diagnostic.message.empty()
                    ? "BearingService: processing diagnostic"
                    : "BearingService: " + diagnostic.message);
    }
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
