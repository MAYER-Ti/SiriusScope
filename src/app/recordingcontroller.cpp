#include "recordingcontroller.h"

#include "appstate.h"
#include "bandconfigcontroller.h"
#include "bandlistmodel.h"
#include "spectrumenvelopecontroller.h"
#include "spectrumenvelopeworker.h"
#include "waterfallcontroller.h"

#include <QMetaObject>

#include <chrono>
#include <string>

namespace siriusscope::app {
namespace {

std::int64_t nowUtcNs()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

} // namespace

RecordingController::RecordingController(hardware::IBcoControl* bcoControl,
                                         BandListModel* bandListModel,
                                         BandConfigController* bandConfigController,
                                         WaterfallController* waterfallController,
                                         SpectrumEnvelopeController* spectrumEnvelopeController,
                                         SpectrumEnvelopeWorker* spectrumEnvelopeWorker,
                                         infrastructure::IDiagnosticsSink* diagnosticsSink,
                                         QObject* parent)
    : QObject(parent)
    , m_bcoControl(bcoControl)
    , m_bandListModel(bandListModel)
    , m_bandConfigController(bandConfigController)
    , m_waterfallController(waterfallController)
    , m_spectrumEnvelopeController(spectrumEnvelopeController)
    , m_spectrumEnvelopeWorker(spectrumEnvelopeWorker)
    , m_diagnosticsSink(diagnosticsSink)
{
}

bool RecordingController::recordingActive() const noexcept
{
    return m_recordingState == RecordingState::Active;
}

bool RecordingController::bcoProcessingActive() const noexcept
{
    return m_bcoProcessingState == hardware::BcoProcessingState::Active;
}

bool RecordingController::canStartRecording() const noexcept
{
    return m_recordingState == RecordingState::Idle || m_recordingState == RecordingState::Failed;
}

bool RecordingController::canStopRecording() const noexcept
{
    return m_recordingState == RecordingState::Active;
}

QString RecordingController::recordingStateText() const
{
    switch (m_recordingState) {
    case RecordingState::Idle:
        return QStringLiteral("выключена");
    case RecordingState::Starting:
        return QStringLiteral("запуск");
    case RecordingState::Active:
        return QStringLiteral("включена");
    case RecordingState::Stopping:
        return QStringLiteral("остановка");
    case RecordingState::Failed:
        return QStringLiteral("ошибка");
    }
    return QStringLiteral("неизвестно");
}

QString RecordingController::bcoProcessingStateText() const
{
    switch (m_bcoProcessingState) {
    case hardware::BcoProcessingState::Idle:
        return QStringLiteral("остановлен");
    case hardware::BcoProcessingState::Starting:
        return QStringLiteral("запуск");
    case hardware::BcoProcessingState::Active:
        return QStringLiteral("поток активен");
    case hardware::BcoProcessingState::Stopping:
        return QStringLiteral("остановка");
    case hardware::BcoProcessingState::Failed:
        return QStringLiteral("ошибка");
    }
    return QStringLiteral("неизвестно");
}

void RecordingController::startRecording()
{
    const auto result = startRecordingCommand();
    if (!result) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "recording start failed: " + result.message);
    }
}

void RecordingController::stopRecording()
{
    if (m_recordingState == RecordingState::Idle || m_recordingState == RecordingState::Failed) {
        return;
    }
    if (m_recordingState == RecordingState::Starting || m_recordingState == RecordingState::Stopping) {
        return;
    }

    setRecordingState(RecordingState::Stopping);
    setBcoProcessingState(hardware::BcoProcessingState::Stopping);

    if (m_waterfallController) {
        m_waterfallController->setAcceptingLiveSamples(false);
        m_waterfallController->clearQueuedBatches();
    }

    core::OperationResult stopResult = core::OperationResult::ok();
    if (m_bcoControl) {
        stopResult = m_bcoControl->stopProcessing();
        if (!stopResult) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "BCO processing stop failed: " + stopResult.message);
        }
    }

    if (m_waterfallController) {
        const auto sourceStopped = m_waterfallController->stopLiveSource();
        if (!sourceStopped) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "live sample source stop failed: " + sourceStopped.message);
        }
        m_waterfallController->flushProcessing(std::chrono::milliseconds{1500});
        m_waterfallController->stopRecording();
    }

    resetSpectrumEnvelope();
    setUiLocked(false);
    setBcoProcessingState(stopResult ? hardware::BcoProcessingState::Idle
                                     : hardware::BcoProcessingState::Failed);
    setRecordingState(stopResult ? RecordingState::Idle : RecordingState::Failed);
    publish(infrastructure::DiagnosticSeverity::Info, "recording stopped");
}

core::OperationResult RecordingController::startRecordingCommand()
{
    if (!canStartRecording()) {
        return core::OperationResult::ok();
    }
    if (!m_bcoControl || !m_bandListModel || !m_waterfallController) {
        setRecordingState(RecordingState::Failed);
        setBcoProcessingState(hardware::BcoProcessingState::Failed);
        return core::OperationResult::failure("recording dependencies are not configured");
    }

    setRecordingState(RecordingState::Starting);
    setBcoProcessingState(hardware::BcoProcessingState::Starting);
    setUiLocked(true);

    const auto sessionId = m_nextSessionId++;
    const auto timeBase = makeTimeBase();
    hardware::BcoProcessingStartCommand command;
    command.bandConfigs = m_bandListModel->bandConfigs();
    command.timeBase = timeBase;
    command.sessionId = sessionId;

    const auto bcoStarted = m_bcoControl->startProcessing(command);
    if (!bcoStarted) {
        cleanupAfterFailedStart();
        return bcoStarted;
    }

    resetSpectrumEnvelope();
    m_waterfallController->setWaterfallTimeBase(timeBase);
    m_waterfallController->startRecording();
    const auto sourceStarted = m_waterfallController->startLiveSource();
    if (!sourceStarted) {
        m_bcoControl->stopProcessing();
        cleanupAfterFailedStart();
        return sourceStarted;
    }

    setBcoProcessingState(hardware::BcoProcessingState::Active);
    setRecordingState(RecordingState::Active);
    publish(infrastructure::DiagnosticSeverity::Info, "recording started");
    return core::OperationResult::ok();
}

void RecordingController::cleanupAfterFailedStart()
{
    if (m_waterfallController) {
        m_waterfallController->setAcceptingLiveSamples(false);
        m_waterfallController->clearQueuedBatches();
        m_waterfallController->stopLiveSource();
        m_waterfallController->stopRecording();
    }
    resetSpectrumEnvelope();
    setUiLocked(false);
    setBcoProcessingState(hardware::BcoProcessingState::Failed);
    setRecordingState(RecordingState::Failed);
}

core::TimeBase RecordingController::makeTimeBase() const
{
    const auto created = core::TimeBase::create(nowUtcNs(),
                                                0,
                                                core::DomainConstraints::defaultSamplePeriodNs);
    if (created) {
        return *created.value();
    }

    return core::TimeBase{};
}

void RecordingController::setRecordingState(RecordingState state)
{
    if (m_recordingState == state) {
        return;
    }
    m_recordingState = state;
    emit recordingStateChanged();
}

void RecordingController::setBcoProcessingState(hardware::BcoProcessingState state)
{
    if (m_bcoProcessingState == state) {
        return;
    }
    m_bcoProcessingState = state;
    emit bcoProcessingStateChanged();
}

void RecordingController::setUiLocked(bool locked)
{
    if (m_bandConfigController) {
        m_bandConfigController->setEditingLocked(locked);
    }
    AppState::instance().setModeChangeLocked(locked);
}

void RecordingController::resetSpectrumEnvelope()
{
    if (m_spectrumEnvelopeWorker) {
        QMetaObject::invokeMethod(m_spectrumEnvelopeWorker,
                                  &SpectrumEnvelopeWorker::reset,
                                  Qt::QueuedConnection);
    }
    if (m_spectrumEnvelopeController) {
        m_spectrumEnvelopeController->clear();
    }
}

void RecordingController::publish(infrastructure::DiagnosticSeverity severity,
                                  const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "RecordingController",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::app
