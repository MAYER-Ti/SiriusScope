#include "statusmodel.h"

#include "appstate.h"
#include "diagnosticsservice.h"
#include "pipeline/data_ingest_pipeline.h"
#include "recordingcontroller.h"
#include "scancontroller.h"
#include "waterfallcontroller.h"

#include <QChar>

#include <cmath>

namespace siriusscope::app {
namespace {

bool isWarning(int severity)
{
    return severity == DiagnosticsService::Warning;
}

bool isError(int severity)
{
    return severity == DiagnosticsService::Error;
}

bool isBcoSubsystem(const QString& subsystem)
{
    return subsystem == QStringLiteral("SimulatorBcoSampleSource")
        || subsystem == QStringLiteral("HighLoadSimulatorBcoStreamSource")
        || subsystem == QStringLiteral("WaterfallProcessing")
        || subsystem == QStringLiteral("DataIngestPipeline")
        || subsystem == QStringLiteral("ProcessingEngine")
        || subsystem == QStringLiteral("PipelineDiagnostics")
        || subsystem == QStringLiteral("SpectrumAggregator")
        || subsystem == QStringLiteral("SpectrumSnapshotAdapter");
}

bool isBcoControlSubsystem(const QString& subsystem)
{
    return subsystem == QStringLiteral("SimulatorBcoControl")
        || subsystem == QStringLiteral("HighLoadSimulatorBcoControl")
        || subsystem == QStringLiteral("BandConfig");
}

bool isAntennaSubsystem(const QString& subsystem)
{
    return subsystem == QStringLiteral("SimulatorAntennaControl")
        || subsystem == QStringLiteral("SimulatorAntennaAzimuthSource")
        || subsystem == QStringLiteral("AntennaController")
        || subsystem == QStringLiteral("ScanController");
}

bool isStorageSubsystem(const QString& subsystem)
{
    return subsystem == QStringLiteral("WaterfallStorage");
}

} // namespace

StatusModel::StatusModel(DiagnosticsService* diagnosticsService,
                         AppState* appState,
                         WaterfallController* waterfallController,
                         RecordingController* recordingController,
                         ScanController* scanController,
                         pipeline::DataIngestPipeline* dataIngestPipeline,
                         QObject* parent)
    : QObject(parent)
    , m_diagnosticsService(diagnosticsService)
    , m_appState(appState)
    , m_waterfallController(waterfallController)
    , m_recordingController(recordingController)
    , m_scanController(scanController)
    , m_dataIngestPipeline(dataIngestPipeline)
{
    updateModeStatus();
    if (!m_recordingController) {
        setBcoStatus(QStringLiteral("источник не задан"), StatusLevel::Warning);
    } else if (m_waterfallController->sourceActive()) {
        updateBcoSourceStatus();
    }
    updateRecordingStatus();
    updateAzimuthStatus();

    if (m_diagnosticsService) {
        connect(m_diagnosticsService,
                &DiagnosticsService::diagnosticEventPublished,
                this,
                &StatusModel::onDiagnosticEvent);
    }

    if (m_appState) {
        connect(m_appState,
                &AppState::modeChanged,
                this,
                [this](AppState::Mode) {
                    updateModeStatus();
                });
    }

    if (m_recordingController) {
        connect(m_recordingController,
                &RecordingController::bcoProcessingStateChanged,
                this,
                &StatusModel::updateBcoSourceStatus);
        connect(m_recordingController,
                &RecordingController::recordingStateChanged,
                this,
                &StatusModel::updateRecordingStatus);
    }

    if (m_scanController) {
        connect(m_scanController,
                &ScanController::currentAzimuthChanged,
                this,
                [this]() {
                    updateAzimuthStatus();
                });
        connect(m_scanController,
                &ScanController::scanStateChanged,
                this,
                [this]() {
                    if (m_scanController && m_scanController->scanActive()) {
                        setAntennaStatus(QStringLiteral("scan active"), StatusLevel::Good);
                    }
                });
    }

    if (m_dataIngestPipeline) {
        m_pipelineMetricsTimer.setInterval(1000);
        connect(&m_pipelineMetricsTimer,
                &QTimer::timeout,
                this,
                &StatusModel::updatePipelineMetrics);
        m_pipelineMetricsTimer.start();
        updatePipelineMetrics();
    }
}

void StatusModel::updateModeStatus()
{
    if (!m_appState) {
        setModeStatus(QStringLiteral("неизвестно"), StatusLevel::Warning);
        return;
    }

    switch (m_appState->mode()) {
    case AppState::Mode::Test:
        setModeStatus(QStringLiteral("генератор"), StatusLevel::Good);
        break;
    case AppState::Mode::Combat:
        setModeStatus(QStringLiteral("аппаратура"), StatusLevel::Good);
        break;
    case AppState::Mode::Control:
        setModeStatus(QStringLiteral("контроль"), StatusLevel::Good);
        break;
    }
}

void StatusModel::updateBcoSourceStatus()
{
    if (!m_recordingController) {
        setBcoStatus(QStringLiteral("источник не задан"), StatusLevel::Warning);
        return;
    }

    setBcoStatus(m_recordingController->bcoProcessingStateText(),
                 m_recordingController->bcoProcessingActive() ? StatusLevel::Good
                                                              : StatusLevel::Neutral);
}

void StatusModel::updateRecordingStatus()
{
    if (!m_recordingController) {
        setRecordingStatus(QStringLiteral("недоступна"), StatusLevel::Warning);
        return;
    }

    setRecordingStatus(m_recordingController->recordingStateText(),
                       m_recordingController->recordingActive() ? StatusLevel::Good
                                                                : StatusLevel::Neutral);
}

void StatusModel::updateAzimuthStatus()
{
    if (!m_scanController) {
        setAzimuthStatus(QStringLiteral("—"), StatusLevel::Warning);
        return;
    }

    setAzimuthStatus(formattedAzimuth(m_scanController->currentAzimuthDeg()), StatusLevel::Good);
}

void StatusModel::updatePipelineMetrics()
{
    if (!m_dataIngestPipeline) {
        return;
    }

    const auto metrics = m_dataIngestPipeline->metricsSnapshot();
    const auto producedSpectrumSnapshots =
        static_cast<qulonglong>(metrics.producedSpectrumSnapshots);
    const auto latestSpectrumSnapshotSequence =
        static_cast<qulonglong>(metrics.latestSpectrumSnapshotSequence);
    const auto spectrumInvalidSamples =
        static_cast<qulonglong>(metrics.spectrumInvalidSamples);
    const auto spectrumOutOfRangeSamples =
        static_cast<qulonglong>(metrics.spectrumOutOfRangeSamples);
    const double spectrumAggregationLatencyMaxMs =
        metrics.spectrumAggregationLatencyMaxMs;

    if (m_producedSpectrumSnapshots == producedSpectrumSnapshots
        && m_latestSpectrumSnapshotSequence == latestSpectrumSnapshotSequence
        && m_spectrumInvalidSamples == spectrumInvalidSamples
        && m_spectrumOutOfRangeSamples == spectrumOutOfRangeSamples
        && m_spectrumAggregationLatencyMaxMs == spectrumAggregationLatencyMaxMs) {
        return;
    }

    m_producedSpectrumSnapshots = producedSpectrumSnapshots;
    m_latestSpectrumSnapshotSequence = latestSpectrumSnapshotSequence;
    m_spectrumInvalidSamples = spectrumInvalidSamples;
    m_spectrumOutOfRangeSamples = spectrumOutOfRangeSamples;
    m_spectrumAggregationLatencyMaxMs = spectrumAggregationLatencyMaxMs;
    emit pipelineMetricsChanged();
}

void StatusModel::onDiagnosticEvent(const QString& subsystem,
                                    int severity,
                                    const QString& message,
                                    qint64)
{
    updateProgramFromDiagnostic(severity);
    updateDiagnosticsFromDiagnostic(severity, message);

    if (isBcoSubsystem(subsystem)) {
        updateBcoFromDiagnostic(subsystem, severity, message);
    } else if (isBcoControlSubsystem(subsystem)) {
        updateBcoControlFromDiagnostic(severity, message);
    } else if (isAntennaSubsystem(subsystem)) {
        updateAntennaFromDiagnostic(severity, message);
    } else if (isStorageSubsystem(subsystem)) {
        updateStorageFromDiagnostic(severity);
    }
}

void StatusModel::updateProgramFromDiagnostic(int severity)
{
    if (isError(severity)) {
        m_programHasError = true;
        setProgramStatus(QStringLiteral("ошибка"), StatusLevel::Error);
        return;
    }

    if (isWarning(severity) && !m_programHasError) {
        setProgramStatus(QStringLiteral("предупреждение"), StatusLevel::Warning);
    }
}

void StatusModel::updateDiagnosticsFromDiagnostic(int severity, const QString& message)
{
    if (isError(severity)) {
        setDiagnosticsStatus(message, StatusLevel::Error);
    } else if (isWarning(severity)) {
        setDiagnosticsStatus(message, StatusLevel::Warning);
    }
}

void StatusModel::updateBcoFromDiagnostic(const QString&,
                                          int severity,
                                          const QString& message)
{
    const QString lower = message.toLower();
    if (isError(severity)) {
        setBcoStatus(QStringLiteral("ошибка потока"), StatusLevel::Error);
    } else if (isWarning(severity)) {
        if (lower.contains(QStringLiteral("dropped"))) {
            setBcoStatus(QStringLiteral("потери очереди"), StatusLevel::Warning);
        } else {
            setBcoStatus(QStringLiteral("предупреждение"), StatusLevel::Warning);
        }
    }
}

void StatusModel::updateBcoControlFromDiagnostic(int severity, const QString& message)
{
    const QString lower = message.toLower();
    if (isError(severity)) {
        setBcoControlStatus(QStringLiteral("ошибка"), StatusLevel::Error);
    } else if (isWarning(severity)) {
        const bool rejected = lower.contains(QStringLiteral("rejected"))
            || lower.contains(QStringLiteral("отклон"));
        setBcoControlStatus(rejected ? QStringLiteral("отклонено")
                                     : QStringLiteral("предупреждение"),
                            StatusLevel::Warning);
    } else if (lower.contains(QStringLiteral("applied"))
               || lower.contains(QStringLiteral("примен"))) {
        setBcoControlStatus(QStringLiteral("применено"), StatusLevel::Good);
    }
}

void StatusModel::updateAntennaFromDiagnostic(int severity, const QString& message)
{
    const QString lower = message.toLower();
    if (isError(severity)) {
        setAntennaStatus(QStringLiteral("ошибка"), StatusLevel::Error);
    } else if (isWarning(severity)) {
        const bool blindZone = lower.contains(QStringLiteral("blind zone"))
            || lower.contains(QStringLiteral("слеп"));
        setAntennaStatus(blindZone ? QStringLiteral("слепая зона")
                                   : QStringLiteral("предупреждение"),
                         StatusLevel::Warning);
    } else if (lower.contains(QStringLiteral("accepted"))) {
        setAntennaStatus(QStringLiteral("команда принята"), StatusLevel::Good);
    } else if (lower.contains(QStringLiteral("scan started"))
               || lower.contains(QStringLiteral("scan active"))) {
        setAntennaStatus(QStringLiteral("scan active"), StatusLevel::Good);
    } else if (lower.contains(QStringLiteral("scan completed"))) {
        setAntennaStatus(QStringLiteral("scan completed"), StatusLevel::Good);
    } else if (lower.contains(QStringLiteral("scan cancelled"))) {
        setAntennaStatus(QStringLiteral("scan cancelled"), StatusLevel::Warning);
    } else if (lower.contains(QStringLiteral("stopped"))) {
        setAntennaStatus(QStringLiteral("остановлен"), StatusLevel::Good);
    }
}

void StatusModel::updateStorageFromDiagnostic(int severity)
{
    if (isError(severity)) {
        setRecordingStatus(QStringLiteral("ошибка записи"), StatusLevel::Error);
    } else if (isWarning(severity)) {
        setRecordingStatus(QStringLiteral("предупреждение"), StatusLevel::Warning);
    }
}

void StatusModel::setProgramStatus(const QString& value, StatusLevel level)
{
    if (m_programValue == value && m_programLevel == level) {
        return;
    }
    m_programValue = value;
    m_programLevel = level;
    emit programStatusChanged();
}

void StatusModel::setModeStatus(const QString& value, StatusLevel level)
{
    if (m_modeValue == value && m_modeLevel == level) {
        return;
    }
    m_modeValue = value;
    m_modeLevel = level;
    emit modeStatusChanged();
}

void StatusModel::setBcoStatus(const QString& value, StatusLevel level)
{
    if (m_bcoValue == value && m_bcoLevel == level) {
        return;
    }
    m_bcoValue = value;
    m_bcoLevel = level;
    emit bcoStatusChanged();
}

void StatusModel::setBcoControlStatus(const QString& value, StatusLevel level)
{
    if (m_bcoControlValue == value && m_bcoControlLevel == level) {
        return;
    }
    m_bcoControlValue = value;
    m_bcoControlLevel = level;
    emit bcoControlStatusChanged();
}

void StatusModel::setAntennaStatus(const QString& value, StatusLevel level)
{
    if (m_antennaValue == value && m_antennaLevel == level) {
        return;
    }
    m_antennaValue = value;
    m_antennaLevel = level;
    emit antennaStatusChanged();
}

void StatusModel::setAzimuthStatus(const QString& value, StatusLevel level)
{
    if (m_azimuthValue == value && m_azimuthLevel == level) {
        return;
    }
    m_azimuthValue = value;
    m_azimuthLevel = level;
    emit azimuthChanged();
}

void StatusModel::setRecordingStatus(const QString& value, StatusLevel level)
{
    if (m_recordingValue == value && m_recordingLevel == level) {
        return;
    }
    m_recordingValue = value;
    m_recordingLevel = level;
    emit recordingStatusChanged();
}

void StatusModel::setDiagnosticsStatus(const QString& value, StatusLevel level)
{
    if (m_diagnosticsValue == value && m_diagnosticsLevel == level) {
        return;
    }
    m_diagnosticsValue = value;
    m_diagnosticsLevel = level;
    emit diagnosticsStatusChanged();
}

QString StatusModel::formattedAzimuth(double azimuthDeg) const
{
    double normalized = std::fmod(azimuthDeg, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    if (normalized >= 360.0) {
        normalized -= 360.0;
    }

    QString text = QStringLiteral("%1").arg(normalized, 5, 'f', 1, QChar(QLatin1Char('0')));
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return text + QChar(0x00B0);
}

} // namespace siriusscope::app
