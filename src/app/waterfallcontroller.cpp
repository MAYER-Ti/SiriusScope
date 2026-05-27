#include "waterfallcontroller.h"

#include "bearingframebus.h"
#include "frequencyviewportmodel.h"
#include "realtimesignalpipeline.h"
#include "signalsamplebus.h"
#include "spectrumenvelopeworker.h"
#include "waterfallringbuffer.h"
#include "waterfallrowresampler.h"

#include <QDateTime>
#include <QMetaObject>
#include <QTimeZone>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <utility>

namespace siriusscope::app {
namespace {

constexpr int kRetuneDelayMs = 160;
constexpr int kRowsPerWheelStep = 5;

QString utcText(qint64 utcMs)
{
    if (utcMs <= 0) {
        return QStringLiteral("UTC --:--:--");
    }

    return QDateTime::fromMSecsSinceEpoch(utcMs, QTimeZone::UTC)
        .toString(QStringLiteral("dd.MM.yyyy HH:mm:ss 'UTC'"));
}

QString sessionLabel(const WaterfallSessionMetadata& metadata)
{
    const QDateTime start =
        QDateTime::fromMSecsSinceEpoch(metadata.startUtcMs, QTimeZone::systemTimeZone());
    const QDateTime end =
        QDateTime::fromMSecsSinceEpoch(metadata.endUtcMs, QTimeZone::systemTimeZone());
    if (metadata.endUtcMs > metadata.startUtcMs) {
        return QStringLiteral("%1-%2")
            .arg(start.toString(QStringLiteral("dd.MM HH:mm:ss")),
                 end.toString(QStringLiteral("HH:mm:ss")));
    }
    return start.toString(QStringLiteral("dd.MM HH:mm:ss"));
}

std::string severityName(processing::ProcessingDiagnosticSeverity severity)
{
    switch (severity) {
    case processing::ProcessingDiagnosticSeverity::Info:
        return "info";
    case processing::ProcessingDiagnosticSeverity::Warning:
        return "warning";
    case processing::ProcessingDiagnosticSeverity::Error:
        return "error";
    }
    return "unknown";
}

infrastructure::DiagnosticSeverity mapSeverity(processing::ProcessingDiagnosticSeverity severity)
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

std::string domainIssueName(core::ValidationCode code)
{
    return std::to_string(static_cast<int>(code));
}

} // namespace

WaterfallController::WaterfallController(FrequencyViewportModel* viewportModel,
                                         hardware::IBcoSampleSource* sampleSource,
                                         std::vector<core::BandConfig> bandConfigs,
                                         IWaterfallSessionStorage* sessionStorage,
                                         infrastructure::IDiagnosticsSink* diagnosticsSink,
                                         WaterfallControllerConfig config,
                                         BearingFrameBus* bearingFrameBus,
                                         SignalSampleBus* signalSampleBus,
                                         SpectrumEnvelopeWorker* spectrumEnvelopeWorker,
                                         QObject* parent)
    : QObject(parent)
    , m_viewportModel(viewportModel)
    , m_sampleSource(sampleSource)
    , m_diagnosticsSink(diagnosticsSink)
    , m_bearingFrameBus(bearingFrameBus)
    , m_signalSampleBus(signalSampleBus)
    , m_spectrumEnvelopeWorker(spectrumEnvelopeWorker)
    , m_ringBuffer(new WaterfallRingBuffer(config.renderBinCount,
                                           config.visibleRowCount,
                                           300e6,
                                           18e9,
                                           this))
    , m_sessionStorage(sessionStorage)
    , m_timelineViewport(config.visibleRowCount, config.sourceFlushIntervalMs)
    , m_controllerConfig(config)
{
    if (!m_sessionStorage) {
        m_ownedSessionStorage = std::make_unique<InMemoryWaterfallSessionStorage>();
        m_sessionStorage = m_ownedSessionStorage.get();
    }

    if (m_viewportModel) {
        connect(m_viewportModel,
                &FrequencyViewportModel::viewportChanged,
                this,
                &WaterfallController::onViewportChanged);
        m_viewMinHz = m_viewportModel->viewMinHz();
        m_viewMaxHz = m_viewportModel->viewMaxHz();
        m_sourceMinHz = m_viewportModel->globalMinHz();
        m_sourceMaxHz = m_viewportModel->globalMaxHz();
    }

    m_processingConfig = makeProcessingConfig(bandConfigs);

    startHistoryWorker();
    reloadHistoryFromStorage();
    updateRenderBuffer();

    m_retuneTimer.setInterval(kRetuneDelayMs);
    m_retuneTimer.setSingleShot(true);
    connect(&m_retuneTimer, &QTimer::timeout, this, &WaterfallController::commitViewport);
}

WaterfallController::~WaterfallController()
{
    stop();
    stopHistoryWorker();
}

QObject* WaterfallController::ringBuffer() const
{
    return m_ringBuffer;
}

bool WaterfallController::liveMode() const noexcept
{
    return m_sessionActive && m_timelineViewport.liveMode();
}

QString WaterfallController::currentUtcText() const
{
    return utcText(m_timelineViewport.topUtcMs());
}

QString WaterfallController::recordingStatusText() const
{
    return m_sessionActive ? QStringLiteral("включена") : QStringLiteral("выключена");
}

QString WaterfallController::recordingLabel() const
{
    const auto metadata = selectedSession();
    if (!metadata) {
        return QStringLiteral("нет сеанса");
    }
    return sessionLabel(*metadata);
}

QString WaterfallController::viewportModeText() const
{
    if (m_sessionActive && m_timelineViewport.liveMode()) {
        return QStringLiteral("live");
    }
    if (m_timelineViewport.hasSession()) {
        return QStringLiteral("history");
    }
    return QStringLiteral("inactive");
}

void WaterfallController::start()
{
    startWorkers();
}

void WaterfallController::stop()
{
    stopLiveSource();
    stopWorkers();
}

void WaterfallController::startWorkers()
{
    {
        std::lock_guard lock(m_workerMutex);
        if (m_workerRunning) {
            return;
        }

        m_stopRequested = false;
        m_workerRunning = true;
    }

    m_worker = std::thread(&WaterfallController::processingLoop, this);
}

core::OperationResult WaterfallController::startLiveSource()
{
    startWorkers();
    if (!m_sampleSource) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "Waterfall sample source is not configured");
        return core::OperationResult::failure("waterfall sample source is not configured");
    }

    if (m_sourceStarted) {
        return core::OperationResult::ok();
    }

    const auto started = m_sampleSource->start([this](const hardware::BcoSampleBatch& batch) {
        enqueueSampleBatch(batch);
    });
    if (!started) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "Waterfall sample source start failed: " + started.message);
        return started;
    }

    m_sourceStarted = true;
    emit sourceActiveChanged();
    publish(infrastructure::DiagnosticSeverity::Info, "BCO sample source started");
    return core::OperationResult::ok();
}

core::OperationResult WaterfallController::stopLiveSource()
{
    if (m_sampleSource && m_sourceStarted) {
        const auto stopped = m_sampleSource->stop();
        m_sourceStarted = false;
        emit sourceActiveChanged();
        if (!stopped) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "Waterfall sample source stop failed: " + stopped.message);
            return stopped;
        }
        publish(infrastructure::DiagnosticSeverity::Info, "BCO sample source stopped");
    }
    return core::OperationResult::ok();
}

void WaterfallController::stopWorkers()
{
    {
        std::lock_guard lock(m_workerMutex);
        if (!m_workerRunning && !m_worker.joinable()) {
            return;
        }
        m_stopRequested = true;
    }

    m_workerCondition.notify_all();

    if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id()) {
        m_worker.join();
    }

    {
        std::lock_guard lock(m_workerMutex);
        m_workerRunning = false;
        m_stopRequested = false;
        m_queuedBatches.clear();
    }
}

void WaterfallController::setAcceptingLiveSamples(bool accepting)
{
    std::lock_guard lock(m_workerMutex);
    m_acceptingLiveSamples = accepting;
}

void WaterfallController::clearQueuedBatches()
{
    std::lock_guard lock(m_workerMutex);
    m_queuedBatches.clear();
    m_droppedBatchCount = 0;
    m_droppedSampleCount = 0;
}

void WaterfallController::setBandConfigs(std::vector<core::BandConfig> bandConfigs)
{
    {
        std::lock_guard lock(m_workerMutex);
        m_processingConfig = makeProcessingConfig(bandConfigs);
        ++m_configRevision;
    }

    m_workerCondition.notify_all();
}

core::OperationResult WaterfallController::flushProcessing(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        return core::OperationResult::failure("processing flush timeout is invalid");
    }

    std::unique_lock lock(m_workerMutex);
    if (!m_workerRunning) {
        return core::OperationResult::failure("processing worker is not running");
    }

    const auto requestId = ++m_flushRequestId;
    m_workerCondition.notify_all();

    const bool completed =
        m_workerCondition.wait_for(lock, timeout, [this, requestId] {
            return m_completedFlushRequestId >= requestId || !m_workerRunning;
        });
    if (!completed || m_completedFlushRequestId < requestId) {
        return core::OperationResult::failure("processing flush timed out");
    }

    return core::OperationResult::ok();
}

void WaterfallController::flushProcessingAsync(std::chrono::milliseconds timeout,
                                               FlushCallback callback)
{
    if (!callback) {
        return;
    }

    if (timeout.count() < 0) {
        callback(core::OperationResult::failure("processing flush timeout is invalid"));
        return;
    }

    std::uint64_t requestId = 0;
    {
        std::lock_guard lock(m_workerMutex);
        if (!m_workerRunning) {
            callback(core::OperationResult::failure("processing worker is not running"));
            return;
        }

        requestId = ++m_flushRequestId;
    }

    {
        std::lock_guard lock(m_asyncFlushMutex);
        m_asyncFlushRequests.emplace_back(requestId, std::move(callback));
    }

    m_workerCondition.notify_all();
    QTimer::singleShot(static_cast<int>(timeout.count()), this, [this, requestId] {
        completeAsyncFlush(requestId,
                           core::OperationResult::failure("processing flush timed out"));
    });
}

void WaterfallController::startHistoryWorker()
{
    std::lock_guard lock(m_historyMutex);
    if (m_historyWorker.joinable()) {
        return;
    }

    m_historyStopRequested = false;
    m_historyWorker = std::thread(&WaterfallController::historyWorkerLoop, this);
}

void WaterfallController::stopHistoryWorker()
{
    {
        std::lock_guard lock(m_historyMutex);
        m_historyStopRequested = true;
        m_pendingHistoryRequest.reset();
    }
    m_historyCondition.notify_all();

    if (m_historyWorker.joinable() && m_historyWorker.get_id() != std::this_thread::get_id()) {
        m_historyWorker.join();
    }

    {
        std::lock_guard lock(m_historyMutex);
        m_historyStopRequested = false;
    }
}

void WaterfallController::historyWorkerLoop()
{
    for (;;) {
        std::optional<HistoryLoadRequest> request;
        {
            std::unique_lock lock(m_historyMutex);
            m_historyCondition.wait(lock, [this] {
                return m_historyStopRequested || m_pendingHistoryRequest.has_value();
            });

            if (m_historyStopRequested) {
                break;
            }

            request = std::move(m_pendingHistoryRequest);
            m_pendingHistoryRequest.reset();
        }

        if (!request) {
            continue;
        }

        auto result = loadHistoryRows(*request);
        QMetaObject::invokeMethod(this,
                                  [this, result = std::move(result)]() mutable {
                                      applyHistoryLoadResult(std::move(result));
                                  },
                                  Qt::QueuedConnection);
    }
}

WaterfallController::HistoryLoadResult WaterfallController::loadHistoryRows(
    const HistoryLoadRequest& request)
{
    HistoryLoadResult result;
    result.requestId = request.requestId;
    result.rowSlots = QVector<WaterfallRowSlot>(request.visibleRowCount);

    if (!m_sessionStorage || !request.sessionId.isValid() || request.visibleRowCount <= 0) {
        return result;
    }

    const WaterfallTimelineMapper mapper(request.topUtcMs,
                                         request.rowPeriodMs,
                                         request.visibleRowCount,
                                         request.visibleRowCount);
    const qint64 marginMs = std::max<qint64>(1, request.rowPeriodMs / 2);
    const auto rows = m_sessionStorage->loadRows(request.sessionId,
                                                 request.bottomUtcMs - marginMs,
                                                 request.topUtcMs + marginMs,
                                                 request.visibleRowCount * 2);

    for (const auto& row : rows) {
        const int slotIndex = mapper.rowIndexForUtcMs(row.utcMs);
        if (slotIndex < 0 || slotIndex >= result.rowSlots.size()) {
            continue;
        }

        WaterfallRow projected = row;
        projected.viewMinHz = request.viewMinHz;
        projected.viewMaxHz = request.viewMaxHz;
        projected.bins = WaterfallRowResampler::resample(row,
                                                         request.viewMinHz,
                                                         request.viewMaxHz,
                                                         request.renderBinCount);
        result.rowSlots[slotIndex] = WaterfallRowSlot{true, std::move(projected)};
    }

    return result;
}

void WaterfallController::applyHistoryLoadResult(HistoryLoadResult result)
{
    if (result.requestId != m_latestHistoryRequestId || !m_ringBuffer) {
        return;
    }

    m_ringBuffer->replaceSlots(result.rowSlots, ++m_generationId);
    ++m_timeTicksVersion;
    emit timeTicksChanged();
    setHistoryLoading(false);
}

QVariantList WaterfallController::visibleTimeTicks(int pixelHeight) const
{
    QVariantList result;
    if (!m_timelineViewport.hasSession()) {
        return result;
    }

    const auto ticks = WaterfallTimelineMapper(m_timelineViewport.topUtcMs(),
                                               m_timelineViewport.rowPeriodMs(),
                                               m_timelineViewport.visibleRowCount(),
                                               pixelHeight)
                           .buildTimeTicks();
    result.reserve(ticks.size());

    for (const auto& tick : ticks) {
        QVariantMap item;
        item.insert(QStringLiteral("y"), tick.y);
        item.insert(QStringLiteral("utcMs"), tick.utcMs);
        item.insert(QStringLiteral("label"), tick.label);
        item.insert(QStringLiteral("major"), tick.major);
        result.push_back(item);
    }

    return result;
}

void WaterfallController::scrollHistory(int wheelSteps)
{
    if (wheelSteps == 0) {
        return;
    }

    const int rows = std::abs(wheelSteps) * kRowsPerWheelStep;
    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    const bool changed = wheelSteps > 0 ? scrollOlder(rows) : scrollTowardLive(rows);

    if (!changed) {
        return;
    }

    updateRenderBuffer();
    notifyPresentationChanged(previousLiveMode, previousUtcText, true);
}

void WaterfallController::jumpToLive()
{
    if (!m_sessionActive || !m_activeSessionId.isValid()) {
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    const auto metadata = m_sessionStorage ? m_sessionStorage->session(m_activeSessionId)
                                           : std::nullopt;
    if (!metadata) {
        return;
    }

    if (!m_timelineViewport.switchToSession(metadata->id,
                                            newestViewportTop(*metadata),
                                            metadata->rowPeriodMs,
                                            WaterfallTimelineViewport::Mode::Live)) {
        return;
    }

    updateRenderBuffer();
    notifyPresentationChanged(previousLiveMode, previousUtcText, true);
}

void WaterfallController::startRecording()
{
    if (m_sessionActive || !m_sessionStorage) {
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    const qint64 nowUtcMs = QDateTime::currentMSecsSinceEpoch();

    WaterfallSessionMetadata metadata;
    metadata.id = WaterfallSessionId{QStringLiteral("session-%1").arg(nowUtcMs)};
    metadata.startUtcMs = nowUtcMs;
    metadata.endUtcMs = nowUtcMs;
    metadata.rowPeriodMs = std::max<qint64>(1, m_controllerConfig.sourceFlushIntervalMs);
    metadata.binCount = m_controllerConfig.renderBinCount;
    metadata.bandCount = static_cast<int>(m_processingConfig.bands.size());
    metadata.beamCount = 2;
    metadata.sourceName = QStringLiteral("BCO");

    metadata = m_sessionStorage->startSession(metadata);
    m_activeSessionId = metadata.id;
    m_sessionActive = true;
    setAcceptingLiveSamples(true);
    m_timelineViewport.switchToSession(metadata.id,
                                       metadata.endUtcMs,
                                       metadata.rowPeriodMs,
                                       WaterfallTimelineViewport::Mode::Live);

    updateRenderBuffer();
    emit recordingStateChanged();
    publish(infrastructure::DiagnosticSeverity::Info, "waterfall recording started");
    notifyPresentationChanged(previousLiveMode, previousUtcText, true);
}

void WaterfallController::stopRecording()
{
    if (!m_sessionActive || !m_sessionStorage || !m_activeSessionId.isValid()) {
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    const auto metadata = m_sessionStorage->session(m_activeSessionId);
    const qint64 endUtcMs = metadata ? metadata->endUtcMs : m_timelineViewport.topUtcMs();

    setAcceptingLiveSamples(false);
    clearQueuedBatches();
    m_sessionStorage->closeSession(m_activeSessionId, endUtcMs);
    m_sessionActive = false;
    m_timelineViewport.setMode(WaterfallTimelineViewport::Mode::History);

    emit recordingStateChanged();
    publish(infrastructure::DiagnosticSeverity::Info, "waterfall recording stopped");
    notifyPresentationChanged(previousLiveMode, previousUtcText, true);
}

void WaterfallController::onViewportChanged(double minHz, double maxHz, const QString&)
{
    scheduleRetune(minHz, maxHz);
}

void WaterfallController::commitViewport()
{
    ++m_generationId;
    m_retuning = false;
    updateRenderBuffer();
}

void WaterfallController::enqueueSampleBatch(const hardware::BcoSampleBatch& batch)
{
    {
        std::lock_guard lock(m_workerMutex);
        if (!m_acceptingLiveSamples) {
            return;
        }

        if (m_controllerConfig.maxQueuedBatches > 0
            && m_queuedBatches.size() >= m_controllerConfig.maxQueuedBatches) {
            const auto& dropped = m_queuedBatches.front();
            ++m_droppedBatchCount;
            m_droppedSampleCount += dropped.samples.size();
            m_queuedBatches.pop_front();
        }

        m_queuedBatches.push_back(batch);
    }

    m_workerCondition.notify_all();

    if (m_spectrumEnvelopeWorker) {
        auto* worker = m_spectrumEnvelopeWorker;
        QMetaObject::invokeMethod(worker,
                                  [worker, batch]() mutable {
                                      worker->ingestBatch(std::move(batch));
                                  },
                                  Qt::QueuedConnection);
    }
}

void WaterfallController::processingLoop()
{
    processing::SampleProcessingConfig config;
    std::size_t workerConfigRevision = 0;
    {
        std::lock_guard lock(m_workerMutex);
        config = m_processingConfig;
        workerConfigRevision = m_configRevision;
    }

    RealtimeSignalPipeline pipeline(RealtimeSignalPipelineConfig{
        config,
        m_signalSampleBus,
        m_bearingFrameBus,
        m_sourceMinHz,
        m_sourceMaxHz,
        m_controllerConfig.renderBinCount,
    });
    std::vector<core::SignalSample> pendingSamples;
    std::size_t pendingEmptyBatches = 0;
    std::uint64_t workerFlushRequestId = 0;
    auto nextFlush = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(std::max(1, m_controllerConfig.sourceFlushIntervalMs));

    for (;;) {
        std::deque<hardware::BcoSampleBatch> batches;
        std::size_t droppedBatches = 0;
        std::size_t droppedSamples = 0;
        bool configChanged = false;
        bool flushRequested = false;
        std::uint64_t flushRequestId = 0;

        {
            std::unique_lock lock(m_workerMutex);
            m_workerCondition.wait_until(lock,
                                         nextFlush,
                                         [this, workerConfigRevision, &workerFlushRequestId] {
                                             return m_stopRequested
                                                 || !m_queuedBatches.empty()
                                                 || m_configRevision != workerConfigRevision
                                                 || m_flushRequestId > workerFlushRequestId;
                                         });

            if (m_stopRequested) {
                break;
            }

            flushRequested = m_flushRequestId > workerFlushRequestId;
            flushRequestId = m_flushRequestId;
            batches.swap(m_queuedBatches);
            droppedBatches = m_droppedBatchCount;
            droppedSamples = m_droppedSampleCount;
            m_droppedBatchCount = 0;
            m_droppedSampleCount = 0;

            if (m_configRevision != workerConfigRevision) {
                config = m_processingConfig;
                workerConfigRevision = m_configRevision;
                configChanged = true;
            }
        }

        const auto completeFlush = [&] {
            if (!flushRequested) {
                return;
            }

            {
                std::lock_guard lock(m_workerMutex);
                workerFlushRequestId = std::max(workerFlushRequestId, flushRequestId);
                m_completedFlushRequestId =
                    std::max(m_completedFlushRequestId, flushRequestId);
            }
            m_workerCondition.notify_all();
            QMetaObject::invokeMethod(this,
                                      [this, flushRequestId] {
                                          completeAsyncFlushesUpTo(flushRequestId);
                                      },
                                      Qt::QueuedConnection);
        };

        if (configChanged) {
            pipeline.setProcessingConfig(config);
            pendingSamples.clear();
            pendingEmptyBatches = 0;
        }

        if (droppedBatches > 0) {
            std::ostringstream message;
            message << "dropped " << droppedBatches
                    << " queued BCO sample batches containing "
                    << droppedSamples << " samples";
            publish(infrastructure::DiagnosticSeverity::Warning, message.str());
        }

        for (auto& batch : batches) {
            if (batch.samples.empty()) {
                ++pendingEmptyBatches;
                continue;
            }

            pendingSamples.insert(pendingSamples.end(),
                                  batch.samples.begin(),
                                  batch.samples.end());
        }

        const auto now = std::chrono::steady_clock::now();
        if (!flushRequested && now < nextFlush) {
            continue;
        }

        nextFlush = now + std::chrono::milliseconds(
            std::max(1, m_controllerConfig.sourceFlushIntervalMs));

        processing::SampleBatch processingBatch;
        processingBatch.samples = std::move(pendingSamples);
        pendingSamples.clear();

        const bool shouldProcessEmptyBatch =
            processingBatch.samples.empty() && pendingEmptyBatches > 0;
        pendingEmptyBatches = 0;
        if (processingBatch.samples.empty() && !shouldProcessEmptyBatch) {
            completeFlush();
            continue;
        }

        try {
            pipeline.setWaterfallRenderContext(m_sourceMinHz,
                                               m_sourceMaxHz,
                                               m_controllerConfig.renderBinCount);
            auto pipelineResult = pipeline.process(RealtimeSignalPipelineInput{
                std::move(processingBatch),
                QDateTime::currentMSecsSinceEpoch(),
            });
            if (pipelineResult.emptyBatchCount > 0) {
                publish(infrastructure::DiagnosticSeverity::Info, "sample batch is empty");
            }

            auto& processingResult = pipelineResult.processingResult;
            publishProcessingDiagnostics(processingResult.diagnostics);

            if (!pipelineResult.renderResult) {
                completeFlush();
                continue;
            }

            QMetaObject::invokeMethod(this,
                                      [this,
                                       result = std::move(*pipelineResult.renderResult)]() mutable {
                                          appendRenderRow(std::move(result));
                                      },
                                      Qt::QueuedConnection);
        } catch (const std::exception& error) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    std::string("processor exception: ") + error.what());
        } catch (...) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "processor exception: unknown failure");
        }

        completeFlush();
    }
}

void WaterfallController::completeAsyncFlushesUpTo(std::uint64_t requestId)
{
    std::vector<FlushCallback> callbacks;
    {
        std::lock_guard lock(m_asyncFlushMutex);
        auto writeIt = m_asyncFlushRequests.begin();
        for (auto readIt = m_asyncFlushRequests.begin(); readIt != m_asyncFlushRequests.end();
             ++readIt) {
            if (readIt->first <= requestId) {
                callbacks.push_back(std::move(readIt->second));
                continue;
            }

            if (writeIt != readIt) {
                *writeIt = std::move(*readIt);
            }
            ++writeIt;
        }
        m_asyncFlushRequests.erase(writeIt, m_asyncFlushRequests.end());
    }

    for (auto& callback : callbacks) {
        callback(core::OperationResult::ok());
    }
}

void WaterfallController::completeAsyncFlush(std::uint64_t requestId,
                                             core::OperationResult result)
{
    FlushCallback callback;
    {
        std::lock_guard lock(m_asyncFlushMutex);
        const auto found =
            std::find_if(m_asyncFlushRequests.begin(),
                         m_asyncFlushRequests.end(),
                         [requestId](const auto& request) {
                             return request.first == requestId;
                         });
        if (found == m_asyncFlushRequests.end()) {
            return;
        }

        callback = std::move(found->second);
        m_asyncFlushRequests.erase(found);
    }

    callback(std::move(result));
}

processing::SampleProcessingConfig WaterfallController::makeProcessingConfig(
    const std::vector<core::BandConfig>& bandConfigs) const
{
    processing::SampleProcessingConfig config;
    config.bands = bandConfigs;
    config.capabilities = core::defaultRuntimeCapabilities();
    config.aggregationWindow.diagnoseMissingWaterfallCells = false;
    return config;
}

void WaterfallController::scheduleRetune(double minHz, double maxHz)
{
    m_viewMinHz = minHz;
    m_viewMaxHz = maxHz;
    m_retuning = true;
    m_retuneTimer.start();
}

void WaterfallController::reloadHistoryFromStorage()
{
    if (!m_sessionStorage) {
        return;
    }

    selectLatestSession();
}

void WaterfallController::setHistoryLoading(bool loading)
{
    if (m_historyLoading == loading) {
        return;
    }
    m_historyLoading = loading;
    emit historyLoadingChanged();
}

void WaterfallController::updateRenderBuffer()
{
    if (!m_ringBuffer) {
        return;
    }

    if (!m_sessionStorage || !m_timelineViewport.hasSession()) {
        m_ringBuffer->replaceSlots(QVector<WaterfallRowSlot>(m_timelineViewport.visibleRowCount()),
                                   ++m_generationId);
        ++m_timeTicksVersion;
        emit timeTicksChanged();
        setHistoryLoading(false);
        return;
    }

    HistoryLoadRequest request;
    request.requestId = ++m_latestHistoryRequestId;
    request.sessionId = m_timelineViewport.sessionId();
    request.topUtcMs = m_timelineViewport.topUtcMs();
    request.bottomUtcMs = m_timelineViewport.bottomUtcMs();
    request.rowPeriodMs = m_timelineViewport.rowPeriodMs();
    request.visibleRowCount = m_timelineViewport.visibleRowCount();
    request.renderBinCount = m_ringBuffer->nbins();
    request.viewMinHz = m_viewMinHz;
    request.viewMaxHz = m_viewMaxHz;

    {
        std::lock_guard lock(m_historyMutex);
        m_pendingHistoryRequest = request;
    }
    setHistoryLoading(true);
    m_historyCondition.notify_one();
}

void WaterfallController::notifyPresentationChanged(bool previousLiveMode,
                                                    const QString& previousUtcText,
                                                    bool viewportDidChange)
{
    if (previousLiveMode != liveMode()) {
        emit liveModeChanged();
    }
    if (previousUtcText != currentUtcText()) {
        emit currentUtcTextChanged();
    }
    if (viewportDidChange) {
        emit viewportChanged();
    }
}

void WaterfallController::appendRenderRow(WaterfallRenderBufferAdapterResult result)
{
    if (!m_sessionActive || !m_sessionStorage || !m_activeSessionId.isValid()) {
        return;
    }

    if (!result.hasVisibleCells) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "waterfall frame without visible cells");
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();

    result.row.sessionId = m_activeSessionId;
    m_sessionStorage->appendRow(m_activeSessionId, result.row);

    if (m_timelineViewport.liveMode()) {
        m_timelineViewport.jumpToLive(result.row.utcMs);
        updateRenderBuffer();
    }

    notifyPresentationChanged(previousLiveMode, previousUtcText, m_timelineViewport.liveMode());
}

bool WaterfallController::selectLatestSession()
{
    if (!m_sessionStorage) {
        return false;
    }

    const auto latest = m_sessionStorage->latestSession();
    if (!latest) {
        return false;
    }

    return switchToSession(*latest, WaterfallTimelineViewport::Mode::History);
}

bool WaterfallController::switchToSession(const WaterfallSessionMetadata& metadata,
                                          WaterfallTimelineViewport::Mode mode)
{
    return m_timelineViewport.switchToSession(metadata.id,
                                              newestViewportTop(metadata),
                                              metadata.rowPeriodMs,
                                              mode);
}

bool WaterfallController::scrollOlder(int rows)
{
    if (!m_sessionStorage || rows <= 0) {
        return false;
    }

    if (!m_timelineViewport.hasSession()) {
        return selectLatestSession();
    }

    const auto metadata = selectedSession();
    if (!metadata) {
        return selectLatestSession();
    }

    const qint64 oldTop = m_timelineViewport.topUtcMs();
    const qint64 minTop = oldestViewportTop(*metadata);
    const qint64 requestedTop = oldTop - static_cast<qint64>(rows) * metadata->rowPeriodMs;
    if (requestedTop >= minTop) {
        return m_timelineViewport.switchToSession(metadata->id,
                                                  requestedTop,
                                                  metadata->rowPeriodMs,
                                                  WaterfallTimelineViewport::Mode::History);
    }

    if (oldTop != minTop) {
        return m_timelineViewport.switchToSession(metadata->id,
                                                  minTop,
                                                  metadata->rowPeriodMs,
                                                  WaterfallTimelineViewport::Mode::History);
    }

    const auto previous = m_sessionStorage->previousSession(metadata->id);
    if (!previous) {
        return false;
    }

    return switchToSession(*previous, WaterfallTimelineViewport::Mode::History);
}

bool WaterfallController::scrollTowardLive(int rows)
{
    if (!m_sessionStorage || rows <= 0 || !m_timelineViewport.hasSession()) {
        return false;
    }

    const auto metadata = selectedSession();
    if (!metadata) {
        return false;
    }

    const qint64 oldTop = m_timelineViewport.topUtcMs();
    const qint64 maxTop = newestViewportTop(*metadata);
    const qint64 requestedTop = oldTop + static_cast<qint64>(rows) * metadata->rowPeriodMs;
    if (requestedTop <= maxTop) {
        const auto mode = m_sessionActive && metadata->id == m_activeSessionId
                && requestedTop == maxTop
            ? WaterfallTimelineViewport::Mode::Live
            : WaterfallTimelineViewport::Mode::History;
        return m_timelineViewport.switchToSession(metadata->id,
                                                  requestedTop,
                                                  metadata->rowPeriodMs,
                                                  mode);
    }

    if (oldTop != maxTop) {
        const auto mode = m_sessionActive && metadata->id == m_activeSessionId
            ? WaterfallTimelineViewport::Mode::Live
            : WaterfallTimelineViewport::Mode::History;
        return m_timelineViewport.switchToSession(metadata->id,
                                                  maxTop,
                                                  metadata->rowPeriodMs,
                                                  mode);
    }

    const auto next = m_sessionStorage->nextSession(metadata->id);
    if (!next) {
        return false;
    }

    const auto mode = m_sessionActive && next->id == m_activeSessionId
        ? WaterfallTimelineViewport::Mode::Live
        : WaterfallTimelineViewport::Mode::History;
    return switchToSession(*next, mode);
}

std::optional<WaterfallSessionMetadata> WaterfallController::selectedSession() const
{
    if (!m_sessionStorage || !m_timelineViewport.hasSession()) {
        return std::nullopt;
    }
    return m_sessionStorage->session(m_timelineViewport.sessionId());
}

qint64 WaterfallController::newestUtcForSession(
    const WaterfallSessionMetadata& metadata) const
{
    return std::max(metadata.startUtcMs, metadata.endUtcMs);
}

qint64 WaterfallController::oldestViewportTop(
    const WaterfallSessionMetadata& metadata) const
{
    const qint64 newestUtcMs = newestViewportTop(metadata);
    const qint64 oldestFullWindowTop =
        metadata.startUtcMs
        + static_cast<qint64>(std::max(0, m_timelineViewport.visibleRowCount() - 1))
            * metadata.rowPeriodMs;
    return std::min(newestUtcMs, std::max(metadata.startUtcMs, oldestFullWindowTop));
}

qint64 WaterfallController::newestViewportTop(
    const WaterfallSessionMetadata& metadata) const
{
    return newestUtcForSession(metadata);
}

void WaterfallController::publish(infrastructure::DiagnosticSeverity severity,
                                  const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "WaterfallProcessing",
        message,
        std::chrono::system_clock::now(),
    });
}

void WaterfallController::publishProcessingDiagnostics(
    const std::vector<processing::ProcessingDiagnostic>& diagnostics) const
{
    for (const auto& diagnostic : diagnostics) {
        publish(mapSeverity(diagnostic.severity), processingDiagnosticMessage(diagnostic));
    }
}

std::string WaterfallController::processingDiagnosticMessage(
    const processing::ProcessingDiagnostic& diagnostic) const
{
    std::ostringstream message;
    message << diagnostic.message
            << " [processingCode=" << static_cast<int>(diagnostic.code)
            << ", severity=" << severityName(diagnostic.severity);

    if (diagnostic.sampleIndex) {
        message << ", sampleIndex=" << *diagnostic.sampleIndex;
    }
    if (diagnostic.bandIndex) {
        message << ", bandIndex=" << *diagnostic.bandIndex;
    }
    if (diagnostic.beamIndex) {
        message << ", beamIndex=" << *diagnostic.beamIndex;
    }
    if (diagnostic.frequencyHz) {
        message << ", frequencyHz=" << *diagnostic.frequencyHz;
    }
    if (!diagnostic.domainIssues.empty()) {
        message << ", domainIssues=";
        for (std::size_t i = 0; i < diagnostic.domainIssues.size(); ++i) {
            if (i > 0) {
                message << '|';
            }
            message << domainIssueName(diagnostic.domainIssues[i].code);
        }
    }

    message << ']';
    return message.str();
}

} // namespace siriusscope::app
