#include "waterfallcontroller.h"

#include "frequencyviewportmodel.h"
#include "waterfallringbuffer.h"
#include "waterfallrowresampler.h"

#include <QDateTime>
#include <QMetaObject>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <limits>
#include <sstream>
#include <utility>

namespace siriusscope::app {
namespace {

constexpr int kRetuneDelayMs = 160;
constexpr int kRowsPerWheelStep = 5;
constexpr int kMaxLoadedRows = 2400;

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
                                         infrastructure::IDiagnosticsSink* diagnosticsSink,
                                         WaterfallControllerConfig config,
                                         QObject* parent)
    : QObject(parent)
    , m_viewportModel(viewportModel)
    , m_sampleSource(sampleSource)
    , m_diagnosticsSink(diagnosticsSink)
    , m_ringBuffer(new WaterfallRingBuffer(config.renderBinCount,
                                           config.visibleRowCount,
                                           300e6,
                                           18e9,
                                           this))
    , m_storage(std::make_unique<InMemoryWaterfallStorage>())
    , m_historyModel(config.visibleRowCount)
    , m_controllerConfig(config)
{
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

    reloadHistoryFromStorage();
    updateRenderBuffer();

    m_retuneTimer.setInterval(kRetuneDelayMs);
    m_retuneTimer.setSingleShot(true);
    connect(&m_retuneTimer, &QTimer::timeout, this, &WaterfallController::commitViewport);
}

WaterfallController::~WaterfallController()
{
    stop();
}

QObject* WaterfallController::ringBuffer() const
{
    return m_ringBuffer;
}

bool WaterfallController::liveMode() const noexcept
{
    return m_historyModel.liveMode();
}

QString WaterfallController::currentUtcText() const
{
    return m_historyModel.currentUtcText();
}

void WaterfallController::start()
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

    if (!m_sampleSource) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "Waterfall sample source is not configured");
        return;
    }

    const auto started = m_sampleSource->start([this](const hardware::BcoSampleBatch& batch) {
        enqueueSampleBatch(batch);
    });
    if (!started) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "Waterfall sample source start failed: " + started.message);
        stop();
        return;
    }

    m_sourceStarted = true;
}

void WaterfallController::stop()
{
    if (m_sampleSource && m_sourceStarted) {
        m_sampleSource->stop();
        m_sourceStarted = false;
    }

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

void WaterfallController::setBandConfigs(std::vector<core::BandConfig> bandConfigs)
{
    {
        std::lock_guard lock(m_workerMutex);
        m_processingConfig = makeProcessingConfig(bandConfigs);
        ++m_configRevision;
    }

    m_workerCondition.notify_all();
}

QVariantList WaterfallController::visibleTimeTicks(int pixelHeight) const
{
    QVariantList result;
    const auto ticks = m_historyModel.visibleTimeTicks(pixelHeight);
    result.reserve(ticks.size());

    for (const auto& tick : ticks) {
        QVariantMap item;
        item.insert(QStringLiteral("y"), tick.y);
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
    const int signedRows = wheelSteps > 0 ? rows : -rows;
    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();

    if (!m_historyModel.scrollRows(signedRows)) {
        return;
    }

    updateRenderBuffer();
    notifyPresentationChanged(previousLiveMode, previousUtcText);
}

void WaterfallController::jumpToLive()
{
    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();

    if (!m_historyModel.jumpToLive()) {
        return;
    }

    updateRenderBuffer();
    notifyPresentationChanged(previousLiveMode, previousUtcText);
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

    processing::SampleProcessor processor(config);
    std::vector<core::SignalSample> pendingSamples;
    std::size_t pendingEmptyBatches = 0;
    auto nextFlush = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(std::max(1, m_controllerConfig.sourceFlushIntervalMs));

    for (;;) {
        std::deque<hardware::BcoSampleBatch> batches;
        std::size_t droppedBatches = 0;
        std::size_t droppedSamples = 0;
        bool configChanged = false;

        {
            std::unique_lock lock(m_workerMutex);
            m_workerCondition.wait_until(lock, nextFlush, [this, workerConfigRevision] {
                return m_stopRequested
                    || !m_queuedBatches.empty()
                    || m_configRevision != workerConfigRevision;
            });

            if (m_stopRequested) {
                break;
            }

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

        if (configChanged) {
            processor = processing::SampleProcessor(config);
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
        if (now < nextFlush) {
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
            continue;
        }

        try {
            auto processingResult = processor.processBatch(processingBatch);
            publishProcessingDiagnostics(processingResult.diagnostics);

            if (processingResult.waterfallFrame.rows.empty()) {
                continue;
            }

            auto renderResult = WaterfallRenderBufferAdapter::adaptFrame(
                processingResult.waterfallFrame,
                QDateTime::currentMSecsSinceEpoch(),
                m_sourceMinHz,
                m_sourceMaxHz,
                m_controllerConfig.renderBinCount);

            QMetaObject::invokeMethod(this,
                                      [this, result = std::move(renderResult)]() mutable {
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
    }
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
    if (!m_storage) {
        return;
    }

    const auto rows = m_storage->loadRows(0,
                                          std::numeric_limits<qint64>::max(),
                                          kMaxLoadedRows);
    m_historyModel.setRows(rows);
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

    const QVector<WaterfallRow> visibleRows = m_historyModel.visibleRows();
    QVector<WaterfallRow> projectedRows;
    projectedRows.reserve(visibleRows.size());

    for (const auto& row : visibleRows) {
        WaterfallRow projected = row;
        projected.viewMinHz = m_viewMinHz;
        projected.viewMaxHz = m_viewMaxHz;
        projected.bins = WaterfallRowResampler::resample(row,
                                                         m_viewMinHz,
                                                         m_viewMaxHz,
                                                         m_ringBuffer->nbins());
        projectedRows.push_back(std::move(projected));
    }

    m_ringBuffer->replaceRows(projectedRows, ++m_generationId);
    ++m_timeTicksVersion;
    emit timeTicksChanged();
}

void WaterfallController::notifyPresentationChanged(bool previousLiveMode,
                                                    const QString& previousUtcText)
{
    if (previousLiveMode != liveMode()) {
        emit liveModeChanged();
    }
    if (previousUtcText != currentUtcText()) {
        emit currentUtcTextChanged();
    }
}

void WaterfallController::appendRenderRow(WaterfallRenderBufferAdapterResult result)
{
    if (!result.hasVisibleCells) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "waterfall frame without visible cells");
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();

    m_storage->appendRow(result.row);
    m_historyModel.appendLiveRow(result.row);

    if (m_historyModel.liveMode()) {
        updateRenderBuffer();
    }

    notifyPresentationChanged(previousLiveMode, previousUtcText);
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
