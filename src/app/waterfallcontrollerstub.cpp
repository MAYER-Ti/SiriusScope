/*!
 *  \file waterfallcontrollerstub.cpp
 *  \brief Реализация генератора-заглушки данных Waterfall.
 */
#include "waterfallcontrollerstub.h"

#include "frequencyviewportmodel.h"
#include "waterfallringbuffer.h"

#include <QRandomGenerator>
#include <QtMath>

namespace {
constexpr int kDefaultBins = 1024;
constexpr int kDefaultRows = 360;
constexpr int kLineIntervalMs = 33;
constexpr int kRetuneDelayMs = 160;
}

WaterfallControllerStub::WaterfallControllerStub(FrequencyViewportModel *viewportModel,
                                                 QObject *parent)
    : QObject(parent)
    , m_viewportModel(viewportModel)
    , m_ringBuffer(new WaterfallRingBuffer(kDefaultBins, kDefaultRows, 300e6, 18e9, this))
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

    m_retuneTimer.setInterval(kRetuneDelayMs);
    m_retuneTimer.setSingleShot(true);
    connect(&m_retuneTimer, &QTimer::timeout, this, &WaterfallControllerStub::commitViewport);

    m_lineTimer.setInterval(kLineIntervalMs);
    m_lineTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_lineTimer, &QTimer::timeout, this, &WaterfallControllerStub::pushSyntheticLine);
    m_lineTimer.start();
}

QObject *WaterfallControllerStub::ringBuffer() const
{
    return m_ringBuffer;
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
}

void WaterfallControllerStub::pushSyntheticLine()
{
    if (!m_ringBuffer || m_lineBuffer.isEmpty()) {
        return;
    }
    if (m_retuning) {
        return;
    }
    buildLine(m_viewMinHz, m_viewMaxHz);
    m_ringBuffer->pushLine(m_lineBuffer.constData(), m_lineBuffer.size(), m_generationId);
}

void WaterfallControllerStub::buildLine(double minHz, double maxHz)
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
}
