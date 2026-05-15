/*!
 *  \file waterfallcontrollerstub.cpp
 *  \brief Реализация генератора-заглушки данных Waterfall.
 */
#include "waterfallcontrollerstub.h"

#include "frequencyviewportmodel.h"
#include "waterfallringbuffer.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QVariantMap>
#include <QtMath>

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
    , m_historyModel(kDefaultRows)
{
    if (m_viewportModel) {
        connect(m_viewportModel,
                &FrequencyViewportModel::viewportChanged,
                this,
                &WaterfallControllerStub::onViewportChanged);
        m_viewMinHz = m_viewportModel->viewMinHz();
        m_viewMaxHz = m_viewportModel->viewMaxHz();
    }

    m_lineBuffer.resize(m_ringBuffer->nbins());
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
    return m_historyModel.liveMode();
}

QString WaterfallControllerStub::currentUtcText() const
{
    return m_historyModel.currentUtcText();
}

QVariantList WaterfallControllerStub::visibleTimeTicks(int pixelHeight) const
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

void WaterfallControllerStub::scrollHistory(int wheelSteps)
{
    if (wheelSteps == 0) {
        return;
    }

    const int rows = std::abs(wheelSteps) * kRowsPerWheelStep;
    const int signedRows = wheelSteps > 0 ? rows : -rows;
    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();

    if (wheelSteps > 0) {
        setHistoryLoading(true);
        QMetaObject::invokeMethod(this, [this, signedRows, previousLiveMode, previousUtcText]() {
            reloadHistoryFromStorage();
            m_historyModel.scrollRows(signedRows);
            updateRenderBuffer();
            notifyPresentationChanged(previousLiveMode, previousUtcText);
            setHistoryLoading(false);
        }, Qt::QueuedConnection);
        return;
    }

    m_historyModel.scrollRows(signedRows);
    updateRenderBuffer();
    notifyPresentationChanged(previousLiveMode, previousUtcText);
}

void WaterfallControllerStub::jumpToLive()
{
    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();

    reloadHistoryFromStorage();
    m_historyModel.jumpToLive();
    updateRenderBuffer();
    notifyPresentationChanged(previousLiveMode, previousUtcText);
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
    if (!m_ringBuffer || m_lineBuffer.isEmpty()) {
        return;
    }
    if (m_retuning) {
        return;
    }
    const bool previousLiveMode = liveMode();
    const QString previousUtcText = currentUtcText();
    const WaterfallRow row = buildLine(m_viewMinHz,
                                       m_viewMaxHz,
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
    const double span = qMax(1.0, maxHz - minHz);
    const int bins = m_lineBuffer.size();

    m_phase += 0.05;
    const double centerA = minHz + span * (0.25 + 0.05 * qSin(m_phase * 0.7));
    const double centerB = minHz + span * (0.62 + 0.07 * qCos(m_phase * 0.4));
    const double widthA = span * 0.045;
    const double widthB = span * 0.06;

    for (int i = 0; i < bins; ++i) {
        const double freq = minHz + (static_cast<double>(i) / qMax(1, bins - 1)) * span;
        const double noise = 0.08 + QRandomGenerator::global()->generateDouble() * 0.10;
        const double peakA = qExp(-qPow((freq - centerA) / widthA, 2.0));
        const double peakB = 0.8 * qExp(-qPow((freq - centerB) / widthB, 2.0));
        double value = (noise + peakA + peakB) * 0.9;

        value = qBound(0.0, value, 1.0);
        m_lineBuffer[i] = static_cast<uint16_t>(value * 65535.0);
    }

    WaterfallRow row;
    row.utcMs = utcMs;
    row.firstSampleIndex = m_nextSampleIndex;
    row.lastSampleIndex = m_nextSampleIndex;
    row.viewMinHz = minHz;
    row.viewMaxHz = maxHz;
    row.bins = m_lineBuffer;
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

void WaterfallControllerStub::updateRenderBuffer()
{
    if (!m_ringBuffer) {
        return;
    }

    m_ringBuffer->replaceRows(m_historyModel.visibleRows(), ++m_generationId);
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
        seedRows.push_back(buildLine(m_viewMinHz, m_viewMaxHz, oldSessionStart + i * kOneSecondMs));
    }
    for (int i = 0; i < 420; ++i) {
        seedRows.push_back(buildLine(m_viewMinHz, m_viewMaxHz, currentStart + i * kOneSecondMs));
    }

    m_storage->appendRows(seedRows);
}
