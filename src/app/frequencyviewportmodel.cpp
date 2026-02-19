/*!
 *  \file frequencyviewportmodel.cpp
 *  \brief Реализация FrequencyViewportModel.
 */
#include "frequencyviewportmodel.h"

#include <QtMath>

//! \brief Конструирует модель и применяет начальные границы обзора.
FrequencyViewportModel::FrequencyViewportModel(QObject *parent)
    : QObject(parent)
{
    applyViewport(m_viewMinHz, m_viewMaxHz, QStringLiteral("init"));
}

/*!
 *  \brief Устанавливает нижнюю границу, сохраняя верхнюю.
 *  \param[in] minHz Нижняя граница видимого диапазона, Гц.
 */
void FrequencyViewportModel::setViewMinHz(double minHz)
{
    setViewport(minHz, m_viewMaxHz, QStringLiteral("qml"));
}

/*!
 *  \brief Устанавливает верхнюю границу, сохраняя нижнюю.
 *  \param[in] maxHz Верхняя граница видимого диапазона, Гц.
 */
void FrequencyViewportModel::setViewMaxHz(double maxHz)
{
    setViewport(m_viewMinHz, maxHz, QStringLiteral("qml"));
}

/*!
 *  \brief Нормализует и применяет полный диапазон.
 *  \param[in] minHz Нижняя граница.
 *  \param[in] maxHz Верхняя граница.
 *  \param[in] sourceTag Источник изменения.
 */
void FrequencyViewportModel::setViewport(double minHz, double maxHz, const QString &sourceTag)
{
    applyViewport(minHz, maxHz, sourceTag);
}

/*!
 *  \brief Применяет ограничения и отправляет сигнал при изменении.
 *  \param[in] minHz Нижняя граница.
 *  \param[in] maxHz Верхняя граница.
 *  \param[in] sourceTag Источник изменения.
 */
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
