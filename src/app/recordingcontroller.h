#pragma once

#include "core/domain_models.h"
#include "hardware/interfaces/bco_control.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <QObject>
#include <QString>

#include <cstdint>

namespace siriusscope::app {

class BandConfigController;
class BandListModel;
class SpectrumEnvelopeController;
class SpectrumEnvelopeWorker;
class WaterfallController;

class RecordingController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recordingActive READ recordingActive NOTIFY recordingStateChanged)
    Q_PROPERTY(bool bcoProcessingActive READ bcoProcessingActive NOTIFY bcoProcessingStateChanged)
    Q_PROPERTY(bool canStartRecording READ canStartRecording NOTIFY recordingStateChanged)
    Q_PROPERTY(bool canStopRecording READ canStopRecording NOTIFY recordingStateChanged)
    Q_PROPERTY(QString recordingStateText READ recordingStateText NOTIFY recordingStateChanged)
    Q_PROPERTY(QString bcoProcessingStateText READ bcoProcessingStateText NOTIFY bcoProcessingStateChanged)

public:
    explicit RecordingController(hardware::IBcoControl* bcoControl,
                                 BandListModel* bandListModel,
                                 BandConfigController* bandConfigController,
                                 WaterfallController* waterfallController,
                                 SpectrumEnvelopeController* spectrumEnvelopeController,
                                 SpectrumEnvelopeWorker* spectrumEnvelopeWorker,
                                 infrastructure::IDiagnosticsSink* diagnosticsSink,
                                 QObject* parent = nullptr);

    bool recordingActive() const noexcept;
    bool bcoProcessingActive() const noexcept;
    bool canStartRecording() const noexcept;
    bool canStopRecording() const noexcept;
    QString recordingStateText() const;
    QString bcoProcessingStateText() const;

    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

signals:
    void recordingStateChanged();
    void bcoProcessingStateChanged();

private:
    enum class RecordingState
    {
        Idle,
        Starting,
        Active,
        Stopping,
        Failed,
    };

    core::OperationResult startRecordingCommand();
    void cleanupAfterFailedStart();
    core::TimeBase makeTimeBase() const;
    void setRecordingState(RecordingState state);
    void setBcoProcessingState(hardware::BcoProcessingState state);
    void setUiLocked(bool locked);
    void resetSpectrumEnvelope();
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    hardware::IBcoControl* m_bcoControl = nullptr;
    BandListModel* m_bandListModel = nullptr;
    BandConfigController* m_bandConfigController = nullptr;
    WaterfallController* m_waterfallController = nullptr;
    SpectrumEnvelopeController* m_spectrumEnvelopeController = nullptr;
    SpectrumEnvelopeWorker* m_spectrumEnvelopeWorker = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    RecordingState m_recordingState = RecordingState::Idle;
    hardware::BcoProcessingState m_bcoProcessingState = hardware::BcoProcessingState::Idle;
    std::uint64_t m_nextSessionId = 1;
};

} // namespace siriusscope::app
