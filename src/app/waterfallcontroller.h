#pragma once

#include "core/domain_models.h"
#include "hardware/interfaces/bco_sample_source.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "processing/sample_processor.h"
#include "waterfallhistorymodel.h"
#include "waterfallrenderbufferadapter.h"
#include "waterfallstorage.h"

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
#include <string>
#include <thread>
#include <vector>

class FrequencyViewportModel;
class WaterfallRingBuffer;

namespace siriusscope::app {

struct WaterfallControllerConfig
{
    int renderBinCount = 1024;
    int visibleRowCount = 360;
    int sourceFlushIntervalMs = 1000;
    std::size_t maxQueuedBatches = 32;
};

class WaterfallController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *ringBuffer READ ringBuffer CONSTANT)
    Q_PROPERTY(bool liveMode READ liveMode NOTIFY liveModeChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyLoadingChanged)
    Q_PROPERTY(QString currentUtcText READ currentUtcText NOTIFY currentUtcTextChanged)
    Q_PROPERTY(qulonglong timeTicksVersion READ timeTicksVersion NOTIFY timeTicksChanged)
    Q_PROPERTY(bool directionalEnabled READ directionalEnabled NOTIFY colorParamsChanged)
    Q_PROPERTY(double colorGamma READ colorGamma NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionDeadZone READ directionDeadZone NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionalAlpha READ directionalAlpha NOTIFY colorParamsChanged)

public:
    explicit WaterfallController(FrequencyViewportModel* viewportModel,
                                 hardware::IBcoSampleSource* sampleSource,
                                 std::vector<core::BandConfig> bandConfigs,
                                 infrastructure::IDiagnosticsSink* diagnosticsSink,
                                 WaterfallControllerConfig config = {},
                                 QObject* parent = nullptr);
    ~WaterfallController() override;

    QObject* ringBuffer() const;
    bool liveMode() const noexcept;
    bool historyLoading() const noexcept { return m_historyLoading; }
    QString currentUtcText() const;
    qulonglong timeTicksVersion() const noexcept { return m_timeTicksVersion; }
    bool directionalEnabled() const noexcept { return true; }
    double colorGamma() const noexcept { return 0.7; }
    double directionDeadZone() const noexcept { return 0.10; }
    double directionalAlpha() const noexcept { return 0.35; }

    void start();
    void stop();
    void setBandConfigs(std::vector<core::BandConfig> bandConfigs);

    Q_INVOKABLE QVariantList visibleTimeTicks(int pixelHeight) const;
    Q_INVOKABLE void scrollHistory(int wheelSteps);
    Q_INVOKABLE void jumpToLive();

signals:
    void liveModeChanged();
    void historyLoadingChanged();
    void currentUtcTextChanged();
    void timeTicksChanged();
    void colorParamsChanged();

private slots:
    void onViewportChanged(double minHz, double maxHz, const QString& sourceTag);
    void commitViewport();

private:
    void enqueueSampleBatch(const hardware::BcoSampleBatch& batch);
    void processingLoop();
    processing::SampleProcessingConfig makeProcessingConfig(
        const std::vector<core::BandConfig>& bandConfigs) const;
    void scheduleRetune(double minHz, double maxHz);
    void reloadHistoryFromStorage();
    void setHistoryLoading(bool loading);
    void updateRenderBuffer();
    void notifyPresentationChanged(bool previousLiveMode, const QString& previousUtcText);
    void appendRenderRow(WaterfallRenderBufferAdapterResult result);
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;
    void publishProcessingDiagnostics(
        const std::vector<processing::ProcessingDiagnostic>& diagnostics) const;
    std::string processingDiagnosticMessage(
        const processing::ProcessingDiagnostic& diagnostic) const;

    FrequencyViewportModel* m_viewportModel = nullptr;
    hardware::IBcoSampleSource* m_sampleSource = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    WaterfallRingBuffer* m_ringBuffer = nullptr;
    std::unique_ptr<InMemoryWaterfallStorage> m_storage;
    WaterfallHistoryModel m_historyModel;
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

    mutable std::mutex m_workerMutex;
    std::condition_variable m_workerCondition;
    std::thread m_worker;
    std::deque<hardware::BcoSampleBatch> m_queuedBatches;
    processing::SampleProcessingConfig m_processingConfig;
    std::size_t m_configRevision = 0;
    std::size_t m_droppedBatchCount = 0;
    std::size_t m_droppedSampleCount = 0;
    bool m_workerRunning = false;
    bool m_stopRequested = false;
};

} // namespace siriusscope::app
