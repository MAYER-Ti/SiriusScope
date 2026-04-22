/*!
 *  \file spectrumdecimator.cpp
 *  \brief Реализация SpectrumDecimator.
 */
#include "spectrumdecimator.h"

#include <QtMath>
#include <limits>

//! \brief Конструирует декоматор.
SpectrumDecimator::SpectrumDecimator(QObject *parent)
    : QObject(parent)
{}

/*!
 *  \brief Вычисляет min/max для каждой выходной колонки.
 *  \param[in] samples Исходные значения спектра.
 *  \param[in] targetWidth Целевая ширина в пикселях.
 *  \return Список значений вида [min0, max0, min1, max1, ...].
 */
QVariantList SpectrumDecimator::decimateMinMax(const QVariantList &samples, int targetWidth) const
{
    if (samples.isEmpty() || targetWidth <= 0) {
        return {};
    }

    const int sampleCount = samples.size();
    const int width = qMax(1, targetWidth);

    QVariantList out;
    out.reserve(width * 2);

    for (int x = 0; x < width; ++x) {
        const int start = (x * sampleCount) / width;
        const int end = qMax(start + 1, ((x + 1) * sampleCount) / width);

        double minVal = std::numeric_limits<double>::infinity();
        double maxVal = -std::numeric_limits<double>::infinity();

        for (int i = start; i < end; ++i) {
            const double v = samples.at(i).toDouble();
            minVal = qMin(minVal, v);
            maxVal = qMax(maxVal, v);
        }

        out.append(minVal);
        out.append(maxVal);
    }

    return out;
}
