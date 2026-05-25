/*!
 *  \file waterfallcontrollerstub.cpp
 *  \brief Реализация генератора-заглушки данных Waterfall.
 */
#include "waterfallcontrollerstub.h"

#include "frequencyviewportmodel.h"
#include "waterfallringbuffer.h"
#include "waterfallrowresampler.h"

#include <QDateTime>
#include <QTimeZone>
#include <QVariantMap>

#include <cmath>
#include <cstdlib>
#include <limits>

namespace {
constexpr int kDefaultBins = 1024;
constexpr int kDefaultRows = 360;
constexpr int kWaterfallLineIntervalMs = 1000;
constexpr int kRetuneDelayMs = 160;
constexpr int kRowsPerWheelStep = 5;
constexpr int kMaxLoadedRows = 2400;
constexpr qint64 kOneSecondMs = 1000;
constexpr qint64 kOneDayMs = 24 * 60 * 60 * kOneSecondMs;
}

WaterfallControllerStub::WaterfallControllerStub(FrequencyViewportModel *viewportModel,
                                                 QObject *parent)
    : QObject(parent)
    , m_viewportModel(viewportModel)
    , m_ringBuffer(new WaterfallRingBuffer(kDefaultBins, kDefaultRows, 300e6, 18e9, this))
    , m_storage(std::make_unique<InMemoryWaterfallStorage>())
    , m_syntheticBands({
          SyntheticBandRange{3.0e9, 5.0e8},
          SyntheticBandRange{5.795e9, 5.0e8},
          SyntheticBandRange{8.25e9, 5.0e8},
          SyntheticBandRange{9.55e9, 5.0e8},
          SyntheticBandRange{14.25e9, 5.0e8}
      })
    , m_historyModel(kDefaultRows)
{
    if (m_viewportModel) {
        connect(m_viewportModel,
                &FrequencyViewportModel::viewportChanged,
                this,
                &WaterfallControllerStub::onViewportChanged);
        m_viewMinHz = m_viewportModel->viewMinHz();
        m_viewMaxHz = m_viewportModel->viewMaxHz();
        m_sourceMinHz = m_viewportModel->globalMinHz();
        m_sourceMaxHz = m_viewportModel->globalMaxHz();
    }

    seedSyntheticHistory();
    reloadHistoryFromStorage();
    updateRenderBuffer();

    m_retuneTimer.setInterval(kRetuneDelayMs);
    m_retuneTimer.setSingleShot(true);
    connect(&m_retuneTimer, &QTimer::timeout, this, &WaterfallControllerStub::commitViewport);

    m_lineTimer.setInterval(kWaterfallLineIntervalMs);
    m_lineTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_lineTimer, &QTimer::timeout, this, &WaterfallControllerStub::pushSyntheticLine);
    m_lineTimer.start();
}

QObject *WaterfallControllerStub::ringBuffer() const
{
    return m_ringBuffer;
}

bool WaterfallControllerStub::liveMode() const noexcept
{
    return m_sessionActive && m_historyModel.liveMode();
}

QString WaterfallControllerStub::currentUtcText() const
{
    return m_historyModel.currentUtcText();
}

QString WaterfallControllerStub::recordingStatusText() const
{
    return m_sessionActive ? QStringLiteral("включена") : QStringLiteral("выключена");
}

QString WaterfallControllerStub::recordingLabel() const
{
    return m_historyModel.rowCount() > 0 ? m_historyModel.currentUtcText()
                                         : QStringLiteral("нет сеанса");
}

QString WaterfallControllerStub::viewportModeText() const
{
    return m_sessionActive && m_historyModel.liveMode() ? QStringLiteral("live")
                                                        : QStringLiteral("history");
}

qint64 WaterfallControllerStub::viewportTopUtcMs() const
{
    const auto rows = m_historyModel.visibleRows();
    return rows.isEmpty() ? 0 : rows.first().utcMs;
}

qint64 WaterfallControllerStub::viewportBottomUtcMs() const
{
    const auto rows = m_historyModel.visibleRows();
    return rows.isEmpty() ? 0 : rows.last().utcMs;
}

QVariantList WaterfallControllerStub::visibleTimeTicks(int pixelHeight) const
{
    QVariantList result;
    const auto ticks = m_historyModel.visibleTimeTicks(pixelHeight);
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

void WaterfallControllerStub::scrollHistory(int wheelSteps)
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
    emit viewportChanged();
}

void WaterfallControllerStub::jumpToLive()
{
    if (!m_sessionActive) {
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();

    if (!m_historyModel.jumpToLive()) {
        return;
    }

    updateRenderBuffer();
    notifyPresentationChanged(previousLiveMode, previousUtcText);
    emit viewportChanged();
}

void WaterfallControllerStub::startRecording()
{
    if (m_sessionActive) {
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    m_sessionActive = true;
    m_historyModel.setRows({});
    updateRenderBuffer();
    emit recordingStateChanged();
    notifyPresentationChanged(previousLiveMode, previousUtcText);
    emit viewportChanged();
}

void WaterfallControllerStub::stopRecording()
{
    if (!m_sessionActive) {
        return;
    }

    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    m_sessionActive = false;
    emit recordingStateChanged();
    notifyPresentationChanged(previousLiveMode, previousUtcText);
    emit viewportChanged();
}

void WaterfallControllerStub::setSyntheticBand(int bandId,
                                               double centerHz,
                                               double widthHz,
                                               double,
                                               bool enabled)
{
    if (bandId < 0) {
        return;
    }

    if (bandId >= m_syntheticBands.size()) {
        m_syntheticBands.resize(bandId + 1);
    }

    if (!enabled) {
        m_syntheticBands[bandId] = SyntheticBandRange{};
        return;
    }

    if (!std::isfinite(centerHz) || !std::isfinite(widthHz) || widthHz <= 0.0) {
        return;
    }

    m_syntheticBands[bandId] = SyntheticBandRange{centerHz, widthHz};
}

void WaterfallControllerStub::onViewportChanged(double minHz, double maxHz, const QString &)
{
    scheduleRetune(minHz, maxHz);
}

void WaterfallControllerStub::scheduleRetune(double minHz, double maxHz)
{
    m_viewMinHz = minHz;
    m_viewMaxHz = maxHz;
    m_retuning = true;
    m_retuneTimer.start();
}

void WaterfallControllerStub::commitViewport()
{
    ++m_generationId;
    m_retuning = false;
    updateRenderBuffer();
}

void WaterfallControllerStub::pushSyntheticLine()
{
    if (!m_ringBuffer) {
        return;
    }
    if (!m_sessionActive) {
        return;
    }
    if (m_retuning) {
        return;
    }
    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    const WaterfallRow row = buildLine(m_sourceMinHz,
                                       m_sourceMaxHz,
                                       QDateTime::currentMSecsSinceEpoch());
    m_storage->appendRow(row);
    m_historyModel.appendLiveRow(row);

    if (m_historyModel.liveMode()) {
        updateRenderBuffer();
    }
    notifyPresentationChanged(previousLiveMode, previousUtcText);
}

WaterfallRow WaterfallControllerStub::buildLine(double minHz, double maxHz, qint64 utcMs)
{
    WaterfallRow row = m_syntheticSource.nextRow(utcMs, minHz, maxHz, currentBands());
    row.firstSampleIndex = m_nextSampleIndex;
    row.lastSampleIndex = m_nextSampleIndex;
    ++m_nextSampleIndex;
    return row;
}

void WaterfallControllerStub::reloadHistoryFromStorage()
{
    if (!m_storage) {
        return;
    }

    const auto rows = m_storage->loadRows(0,
                                          std::numeric_limits<qint64>::max(),
                                          kMaxLoadedRows);
    m_historyModel.setRows(rows);
}

void WaterfallControllerStub::setHistoryLoading(bool loading)
{
    if (m_historyLoading == loading) {
        return;
    }
    m_historyLoading = loading;
    emit historyLoadingChanged();
}

QVector<SyntheticBandRange> WaterfallControllerStub::currentBands() const
{
    return m_syntheticBands;
}

void WaterfallControllerStub::updateRenderBuffer()
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

void WaterfallControllerStub::notifyPresentationChanged(bool previousLiveMode,
                                                        const QString& previousUtcText)
{
    if (previousLiveMode != liveMode()) {
        emit liveModeChanged();
    }
    if (previousUtcText != currentUtcText()) {
        emit currentUtcTextChanged();
    }
}

void WaterfallControllerStub::seedSyntheticHistory()
{
    if (!m_storage) {
        return;
    }

    QVector<WaterfallRow> seedRows;
    seedRows.reserve(500);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 currentStart = now - 419 * kOneSecondMs;
    const qint64 oldSessionStart = now - kOneDayMs - 79 * kOneSecondMs;

    for (int i = 0; i < 80; ++i) {
        seedRows.push_back(buildLine(m_sourceMinHz, m_sourceMaxHz, oldSessionStart + i * kOneSecondMs));
    }
    for (int i = 0; i < 420; ++i) {
        seedRows.push_back(buildLine(m_sourceMinHz, m_sourceMaxHz, currentStart + i * kOneSecondMs));
    }

    m_storage->appendRows(seedRows);
}
