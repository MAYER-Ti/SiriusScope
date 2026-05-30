#include "waterfallcontroller.h"

#include "frequencyviewportmodel.h"
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
#include <utility>

namespace siriusscope::app {
namespace {

constexpr int kRetuneDelayMs = 160;
constexpr int kRowsPerWheelStep = 5;

std::int64_t nowUtcNs()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

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

} // namespace

WaterfallController::WaterfallController(FrequencyViewportModel* viewportModel,
                                         hardware::IBcoStreamSource* streamSource,
                                         std::vector<core::BandConfig> bandConfigs,
                                         IWaterfallSessionStorage* sessionStorage,
                                         infrastructure::IDiagnosticsSink* diagnosticsSink,
                                         WaterfallControllerConfig config,
                                         BearingFrameBus* bearingFrameBus,
                                         SignalSampleBus* signalSampleBus,
                                         SpectrumEnvelopeWorker* spectrumEnvelopeWorker,
                                         pipeline::DataIngestPipeline* dataIngestPipeline,
                                         QObject* parent)
    : QObject(parent)
    , m_viewportModel(viewportModel)
    , m_streamSource(streamSource)
    , m_diagnosticsSink(diagnosticsSink)
    , m_dataIngestPipeline(dataIngestPipeline)
    , m_ringBuffer(new WaterfallRingBuffer(config.renderBinCount,
                                           config.visibleRowCount,
                                           300e6,
                                           18e9,
                                           this))
    , m_sessionStorage(sessionStorage)
    , m_timelineViewport(config.visibleRowCount, config.sourceFlushIntervalMs)
    , m_controllerConfig(config)
    , m_bandConfigs(std::move(bandConfigs))
{
    (void)bearingFrameBus;
    (void)signalSampleBus;
    (void)spectrumEnvelopeWorker;

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

    startHistoryWorker();
    reloadHistoryFromStorage();
    updateRenderBuffer();

    m_retuneTimer.setInterval(kRetuneDelayMs);
    m_retuneTimer.setSingleShot(true);
    connect(&m_retuneTimer, &QTimer::timeout, this, &WaterfallController::commitViewport);

    m_snapshotTimer.setInterval(std::max(1, m_controllerConfig.snapshotPollIntervalMs));
    connect(&m_snapshotTimer,
            &QTimer::timeout,
            this,
            &WaterfallController::pollWaterfallRows);
    configureDataPlaneWaterfall(fallbackWaterfallTimeBase());
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
    if (!m_snapshotTimer.isActive()) {
        m_snapshotTimer.start();
    }
}

void WaterfallController::stop()
{
    m_snapshotTimer.stop();
    stopLiveSource();
    stopWorkers();
}

void WaterfallController::startWorkers()
{
    if (m_dataIngestPipeline) {
        const auto started = m_dataIngestPipeline->start();
        if (!started) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "Data ingest pipeline start failed: " + started.message);
        }
    }
}

core::OperationResult WaterfallController::startLiveSource()
{
    startWorkers();
    if (!m_streamSource) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "Waterfall stream source is not configured");
        return core::OperationResult::failure("waterfall stream source is not configured");
    }

    if (m_sourceStarted) {
        return core::OperationResult::ok();
    }

    if (m_dataIngestPipeline && !m_dataIngestPipeline->running()) {
        const auto started = m_dataIngestPipeline->start();
        if (!started) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "Data ingest pipeline start failed: " + started.message);
            return started;
        }
    }

    const auto started = m_streamSource->start([this](
                                                   hardware::IBcoStreamSource::SampleBlockPtr block) {
        enqueueSampleBlock(std::move(block));
    });
    if (!started) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "Waterfall stream source start failed: " + started.message);
        return started;
    }

    m_sourceStarted = true;
    emit sourceActiveChanged();
    publish(infrastructure::DiagnosticSeverity::Info, "BCO stream source started");
    return core::OperationResult::ok();
}

core::OperationResult WaterfallController::stopLiveSource()
{
    if (m_streamSource && m_sourceStarted) {
        const auto stopped = m_streamSource->stop();
        m_sourceStarted = false;
        emit sourceActiveChanged();
        if (!stopped) {
            publish(infrastructure::DiagnosticSeverity::Error,
                    "Waterfall stream source stop failed: " + stopped.message);
            return stopped;
        }
        publish(infrastructure::DiagnosticSeverity::Info, "BCO stream source stopped");
    }
    return core::OperationResult::ok();
}

void WaterfallController::stopWorkers()
{
    if (m_dataIngestPipeline) {
        m_dataIngestPipeline->stop();
    }
}

void WaterfallController::setAcceptingLiveSamples(bool accepting)
{
    m_acceptingLiveSamples = accepting;
    if (m_dataIngestPipeline) {
        m_dataIngestPipeline->setAccepting(accepting);
    }
}

void WaterfallController::clearQueuedBatches()
{
    resetLiveRowTimeState();
    if (m_dataIngestPipeline) {
        m_dataIngestPipeline->clearQueuedBlocks();
    }
}

void WaterfallController::setBandConfigs(std::vector<core::BandConfig> bandConfigs)
{
    m_bandConfigs = std::move(bandConfigs);
}

void WaterfallController::setWaterfallTimeBase(core::TimeBase timeBase)
{
    m_waterfallTimeBase = timeBase;
    configureDataPlaneWaterfall(timeBase);
}

core::OperationResult WaterfallController::flushProcessing(std::chrono::milliseconds timeout)
{
    if (timeout.count() < 0) {
        return core::OperationResult::failure("processing flush timeout is invalid");
    }
    if (!m_dataIngestPipeline) {
        return core::OperationResult::ok();
    }

    const auto result = m_dataIngestPipeline->flushProcessing(timeout);
    if (result) {
        pollWaterfallRows();
    }
    return result;
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

    QTimer::singleShot(0, this, [this, timeout, callback = std::move(callback)]() mutable {
        callback(flushProcessing(timeout));
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

    resetLiveRowTimeState();
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
    resetLiveRowTimeState();
    if (!m_waterfallTimeBase) {
        setWaterfallTimeBase(fallbackWaterfallTimeBase());
    }
    const qint64 nowUtcMs = static_cast<qint64>(
        m_waterfallTimeBase->recordingStartUtcNs / 1'000'000);

    WaterfallSessionMetadata metadata;
    metadata.id = WaterfallSessionId{QStringLiteral("session-%1").arg(nowUtcMs)};
    metadata.startUtcMs = nowUtcMs;
    metadata.endUtcMs = nowUtcMs;
    metadata.rowPeriodMs = std::max<qint64>(1, m_controllerConfig.rowPeriodMs);
    metadata.binCount = m_controllerConfig.renderBinCount;
    metadata.bandCount = static_cast<int>(m_bandConfigs.size());
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
    m_waterfallTimeBase.reset();
    resetLiveRowTimeState();
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

void WaterfallController::pollWaterfallRows()
{
    if (!m_sessionActive || !m_dataIngestPipeline) {
        return;
    }

    const auto rows = m_dataIngestPipeline->drainWaterfallRows(
        m_controllerConfig.maxWaterfallRowsPerUiTick);
    if (rows.empty()) {
        return;
    }

    for (const auto& row : rows) {
        auto adapted = WaterfallRenderBufferAdapter::adaptQueuedRow(
            row,
            m_viewMinHz,
            m_viewMaxHz,
            m_controllerConfig.renderBinCount);
        appendRenderRow(std::move(adapted));
    }
}

void WaterfallController::enqueueSampleBlock(hardware::IBcoStreamSource::SampleBlockPtr block)
{
    if (!block || !m_dataIngestPipeline) {
        return;
    }

    pipeline::SignalBlockMetadata metadata;
    metadata.firstSampleIndex = block->stats.firstSampleIndex;
    metadata.lastSampleIndex = block->stats.lastSampleIndex;
    metadata.producedAt = block->stats.producedAt;
    metadata.antennaAzimuthDeg = block->stats.antennaAzimuthDeg;

    const auto result = m_dataIngestPipeline->ingestSamples(block->samples, metadata);
    if (!result && m_acceptingLiveSamples) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "Data ingest rejected BCO block: " + result.message);
    }
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

    resetLiveRowTimeState();

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

    const bool isLive = m_timelineViewport.liveMode();
    if (isLive && m_ringBuffer) {
        m_timelineViewport.jumpToLive(result.row.utcMs);

        bool pushedLiveRows = true;
        const qint64 rowPeriodMs = std::max<qint64>(1, m_timelineViewport.rowPeriodMs());
        if (m_lastLiveRowUtcMs && result.row.utcMs > *m_lastLiveRowUtcMs) {
            const qint64 deltaMs = result.row.utcMs - *m_lastLiveRowUtcMs;
            qint64 missingRows = deltaMs / rowPeriodMs - 1;
            const qint64 maxGapRows =
                std::max<qint64>(0, static_cast<qint64>(m_ringBuffer->height()) - 1);
            missingRows = std::clamp<qint64>(missingRows, 0, maxGapRows);

            for (qint64 i = missingRows; i > 0; --i) {
                const qint64 emptyUtcMs = result.row.utcMs - i * rowPeriodMs;
                if (!pushLiveRowToRingBuffer(makeEmptyLiveRow(emptyUtcMs, result.row))) {
                    pushedLiveRows = false;
                    break;
                }
            }
        }

        if (pushedLiveRows && pushLiveRowToRingBuffer(result.row)) {
            if (!m_lastLiveRowUtcMs || result.row.utcMs > *m_lastLiveRowUtcMs) {
                m_lastLiveRowUtcMs = result.row.utcMs;
            }
            ++m_timeTicksVersion;
            emit timeTicksChanged();

            notifyPresentationChanged(previousLiveMode, previousUtcText, true);
            return;
        }

        publish(infrastructure::DiagnosticSeverity::Warning,
                "waterfall live row rejected: bin count mismatch");
        updateRenderBuffer();
    } else if (isLive) {
        m_timelineViewport.jumpToLive(result.row.utcMs);
        updateRenderBuffer();
    }

    notifyPresentationChanged(previousLiveMode, previousUtcText, isLive);
}

void WaterfallController::resetLiveRowTimeState() noexcept
{
    m_lastLiveRowUtcMs.reset();
}

WaterfallRow WaterfallController::makeEmptyLiveRow(qint64 utcMs,
                                                   const WaterfallRow& referenceRow) const
{
    WaterfallRow row;
    row.sessionId = referenceRow.sessionId;
    row.utcMs = utcMs;
    row.firstSampleIndex = 0;
    row.lastSampleIndex = 0;
    row.viewMinHz = referenceRow.viewMinHz;
    row.viewMaxHz = referenceRow.viewMaxHz;
    const int binCount = m_ringBuffer
        ? m_ringBuffer->nbins()
        : static_cast<int>(referenceRow.bins.size());
    row.bins = QVector<WaterfallBeamBin>(std::max(0, binCount), WaterfallBeamBin{});
    return row;
}

bool WaterfallController::pushLiveRowToRingBuffer(const WaterfallRow& row)
{
    if (!m_ringBuffer || m_ringBuffer->height() <= 0 || row.bins.isEmpty()) {
        return false;
    }

    const int rowBinCount = static_cast<int>(row.bins.size());
    if (rowBinCount != m_ringBuffer->nbins()) {
        return false;
    }

    m_ringBuffer->pushLine(row.bins.constData(), rowBinCount, ++m_generationId);
    return true;
}

void WaterfallController::configureDataPlaneWaterfall(core::TimeBase timeBase)
{
    if (!m_dataIngestPipeline) {
        return;
    }

    pipeline::WaterfallAggregatorConfig config;
    config.renderBinCount = m_controllerConfig.renderBinCount;
    config.sourceMinHz = static_cast<std::int64_t>(m_sourceMinHz);
    config.sourceMaxHz = static_cast<std::int64_t>(m_sourceMaxHz);
    config.rowPeriodNs =
        static_cast<std::uint64_t>(std::max<qint64>(1, m_controllerConfig.rowPeriodMs))
        * 1'000'000ULL;
    config.amplitudeFloor = 0;
    config.timeBase = timeBase;
    m_dataIngestPipeline->configureWaterfall(config);

    pipeline::BearingAggregatorConfig bearingConfig;
    bearingConfig.frequencyBinCount = m_controllerConfig.renderBinCount;
    bearingConfig.sourceMinHz = static_cast<std::int64_t>(m_sourceMinHz);
    bearingConfig.sourceMaxHz = static_cast<std::int64_t>(m_sourceMaxHz);
    bearingConfig.windowPeriodNs =
        static_cast<std::uint64_t>(std::max<qint64>(1, m_controllerConfig.rowPeriodMs))
        * 1'000'000ULL;
    bearingConfig.amplitudeFloor = 1;
    bearingConfig.beamHalfSeparationDeg = 30.0;
    bearingConfig.minQuality = 0.05;
    bearingConfig.fallbackAntennaAzimuthDeg = 0.0;
    bearingConfig.timeBase = timeBase;
    m_dataIngestPipeline->configureBearing(bearingConfig);
}

core::TimeBase WaterfallController::fallbackWaterfallTimeBase() const
{
    const auto created = core::TimeBase::create(nowUtcNs(),
                                                0,
                                                core::DomainConstraints::defaultSamplePeriodNs);
    if (created) {
        return *created.value();
    }
    return core::TimeBase{};
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

} // namespace siriusscope::app
