#include "frequencyviewportmodel.h"

#include <QtMath>

FrequencyViewportModel::FrequencyViewportModel(QObject *parent)
    : QObject(parent)
{
    applyViewport(m_viewMinHz, m_viewMaxHz, QStringLiteral("init"));
}

void FrequencyViewportModel::setViewMinHz(double minHz)
{
    setViewport(minHz, m_viewMaxHz, QStringLiteral("qml"));
}

void FrequencyViewportModel::setViewMaxHz(double maxHz)
{
    setViewport(m_viewMinHz, maxHz, QStringLiteral("qml"));
}

void FrequencyViewportModel::setViewport(double minHz, double maxHz, const QString &sourceTag)
{
    applyViewport(minHz, maxHz, sourceTag);
}

void FrequencyViewportModel::applyViewport(double minHz, double maxHz, const QString &sourceTag)
{
    if (maxHz < minHz) {
        qSwap(minHz, maxHz);
    }

    minHz = qBound(m_globalMinHz, minHz, m_globalMaxHz);
    maxHz = qBound(m_globalMinHz, maxHz, m_globalMaxHz);

    if (!qFuzzyCompare(minHz + 1.0, m_viewMinHz + 1.0)
        || !qFuzzyCompare(maxHz + 1.0, m_viewMaxHz + 1.0)) {
        m_viewMinHz = minHz;
        m_viewMaxHz = maxHz;
        emit viewportChanged(m_viewMinHz, m_viewMaxHz, sourceTag);
    }
}
