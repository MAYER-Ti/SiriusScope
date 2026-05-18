#pragma once

#include "app/interfaces/processing_flush_control.h"
#include "core/domain_models.h"
#include "hardware/interfaces/bco_sample_source.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "processing/sample_processor.h"
#include "waterfallrenderbufferadapter.h"
#include "waterfallstorage.h"
#include "waterfalltimeline.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class FrequencyViewportModel;
class WaterfallRingBuffer;

namespace siriusscope::app {

class BearingFrameBus;

struct WaterfallControllerConfig
{
    int renderBinCount = 1024;
    int visibleRowCount = 360;
    int sourceFlushIntervalMs = 1000;
    std::size_t maxQueuedBatches = 32;
};

class WaterfallController final : public QObject, public IProcessingFlushControl
{
    Q_OBJECT
    Q_PROPERTY(QObject *ringBuffer READ ringBuffer CONSTANT)
    Q_PROPERTY(bool liveMode READ liveMode NOTIFY liveModeChanged)
    Q_PROPERTY(bool sourceActive READ sourceActive NOTIFY sourceActiveChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyLoadingChanged)
    Q_PROPERTY(QString currentUtcText READ currentUtcText NOTIFY currentUtcTextChanged)
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY recordingStateChanged)
    Q_PROPERTY(QString recordingStatusText READ recordingStatusText NOTIFY recordingStateChanged)
    Q_PROPERTY(QString recordingLabel READ recordingLabel NOTIFY viewportChanged)
    Q_PROPERTY(QString viewportModeText READ viewportModeText NOTIFY viewportChanged)
    Q_PROPERTY(qint64 viewportTopUtcMs READ viewportTopUtcMs NOTIFY viewportChanged)
    Q_PROPERTY(qint64 viewportBottomUtcMs READ viewportBottomUtcMs NOTIFY viewportChanged)
    Q_PROPERTY(int visibleRowCount READ visibleRowCount CONSTANT)
    Q_PROPERTY(qulonglong timeTicksVersion READ timeTicksVersion NOTIFY timeTicksChanged)
    Q_PROPERTY(bool directionalEnabled READ directionalEnabled NOTIFY colorParamsChanged)
    Q_PROPERTY(int displayAmplitudeThreshold READ displayAmplitudeThreshold NOTIFY colorParamsChanged)
    Q_PROPERTY(double colorGamma READ colorGamma NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionDeadZone READ directionDeadZone NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionalAlpha READ directionalAlpha NOTIFY colorParamsChanged)

public:
    explicit WaterfallController(FrequencyViewportModel* viewportModel,
                                 hardware::IBcoSampleSource* sampleSource,
                                 std::vector<core::BandConfig> bandConfigs,
                                 IWaterfallSessionStorage* sessionStorage,
                                 infrastructure::IDiagnosticsSink* diagnosticsSink,
                                 WaterfallControllerConfig config = {},
                                 BearingFrameBus* bearingFrameBus = nullptr,
                                 QObject* parent = nullptr);
    ~WaterfallController() override;

    QObject* ringBuffer() const;
    bool liveMode() const noexcept;
    bool sourceActive() const noexcept { return m_sourceStarted; }
    bool historyLoading() const noexcept { return m_historyLoading; }
    QString currentUtcText() const;
    bool sessionActive() const noexcept { return m_sessionActive; }
    QString recordingStatusText() const;
    QString recordingLabel() const;
    QString viewportModeText() const;
    qint64 viewportTopUtcMs() const noexcept { return m_timelineViewport.topUtcMs(); }
    qint64 viewportBottomUtcMs() const noexcept { return m_timelineViewport.bottomUtcMs(); }
    int visibleRowCount() const noexcept { return m_timelineViewport.visibleRowCount(); }
    qulonglong timeTicksVersion() const noexcept { return m_timeTicksVersion; }
    bool directionalEnabled() const noexcept { return true; }
    int displayAmplitudeThreshold() const noexcept { return 4; }
    double colorGamma() const noexcept { return 0.7; }
    double directionDeadZone() const noexcept { return 0.10; }
    double directionalAlpha() const noexcept { return 0.35; }

    void start();
    void stop();
    void setBandConfigs(std::vector<core::BandConfig> bandConfigs);
    core::OperationResult flushProcessing(std::chrono::milliseconds timeout) override;

    Q_INVOKABLE QVariantList visibleTimeTicks(int pixelHeight) const;
    Q_INVOKABLE void scrollHistory(int wheelSteps);
    Q_INVOKABLE void jumpToLive();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

signals:
    void liveModeChanged();
    void sourceActiveChanged();
    void historyLoadingChanged();
    void currentUtcTextChanged();
    void recordingStateChanged();
    void viewportChanged();
    void timeTicksChanged();
    void colorParamsChanged();

private slots:
    void onViewportChanged(double minHz, double maxHz, const QString& sourceTag);
    void commitViewport();

private:
    struct HistoryLoadRequest
    {
        uint64_t requestId = 0;
        WaterfallSessionId sessionId;
        qint64 topUtcMs = 0;
        qint64 bottomUtcMs = 0;
        qint64 rowPeriodMs = 1000;
        int visibleRowCount = 0;
        int renderBinCount = 0;
        double viewMinHz = 0.0;
        double viewMaxHz = 0.0;
    };

    struct HistoryLoadResult
    {
        uint64_t requestId = 0;
        QVector<WaterfallRowSlot> rowSlots;
    };

    void enqueueSampleBatch(const hardware::BcoSampleBatch& batch);
    void processingLoop();
    void startHistoryWorker();
    void stopHistoryWorker();
    void historyWorkerLoop();
    HistoryLoadResult loadHistoryRows(const HistoryLoadRequest& request);
    void applyHistoryLoadResult(HistoryLoadResult result);
    processing::SampleProcessingConfig makeProcessingConfig(
        const std::vector<core::BandConfig>& bandConfigs) const;
    void scheduleRetune(double minHz, double maxHz);
    void reloadHistoryFromStorage();
    void setHistoryLoading(bool loading);
    void updateRenderBuffer();
    void notifyPresentationChanged(bool previousLiveMode,
                                   const QString& previousUtcText,
                                   bool viewportDidChange);
    void appendRenderRow(WaterfallRenderBufferAdapterResult result);
    bool selectLatestSession();
    bool switchToSession(const WaterfallSessionMetadata& metadata,
                         WaterfallTimelineViewport::Mode mode);
    bool scrollOlder(int rows);
    bool scrollTowardLive(int rows);
    std::optional<WaterfallSessionMetadata> selectedSession() const;
    qint64 newestUtcForSession(const WaterfallSessionMetadata& metadata) const;
    qint64 oldestViewportTop(const WaterfallSessionMetadata& metadata) const;
    qint64 newestViewportTop(const WaterfallSessionMetadata& metadata) const;
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;
    void publishProcessingDiagnostics(
        const std::vector<processing::ProcessingDiagnostic>& diagnostics) const;
    std::string processingDiagnosticMessage(
        const processing::ProcessingDiagnostic& diagnostic) const;

    FrequencyViewportModel* m_viewportModel = nullptr;
    hardware::IBcoSampleSource* m_sampleSource = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    BearingFrameBus* m_bearingFrameBus = nullptr;
    WaterfallRingBuffer* m_ringBuffer = nullptr;
    IWaterfallSessionStorage* m_sessionStorage = nullptr;
    std::unique_ptr<InMemoryWaterfallSessionStorage> m_ownedSessionStorage;
    WaterfallTimelineViewport m_timelineViewport;
    WaterfallSessionId m_activeSessionId;
    WaterfallControllerConfig m_controllerConfig;
    QTimer m_retuneTimer;
    qulonglong m_timeTicksVersion = 0;
    uint64_t m_generationId = 0;
    double m_viewMinHz = 0.0;
    double m_viewMaxHz = 0.0;
    double m_sourceMinHz = 300e6;
    double m_sourceMaxHz = 18e9;
    bool m_retuning = false;
    bool m_historyLoading = false;
    bool m_sourceStarted = false;
    bool m_sessionActive = false;

    mutable std::mutex m_workerMutex;
    std::condition_variable m_workerCondition;
    std::thread m_worker;
    std::deque<hardware::BcoSampleBatch> m_queuedBatches;
    processing::SampleProcessingConfig m_processingConfig;
    std::size_t m_configRevision = 0;
    std::size_t m_droppedBatchCount = 0;
    std::size_t m_droppedSampleCount = 0;
    std::uint64_t m_flushRequestId = 0;
    std::uint64_t m_completedFlushRequestId = 0;
    bool m_workerRunning = false;
    bool m_stopRequested = false;

    mutable std::mutex m_historyMutex;
    std::condition_variable m_historyCondition;
    std::thread m_historyWorker;
    std::optional<HistoryLoadRequest> m_pendingHistoryRequest;
    uint64_t m_latestHistoryRequestId = 0;
    bool m_historyStopRequested = false;
};

} // namespace siriusscope::app
