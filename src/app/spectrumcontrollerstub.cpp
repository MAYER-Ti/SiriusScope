/*!
 *  \file spectrumcontrollerstub.cpp
 *  \brief Реализация SpectrumControllerStub.
 */
#include "spectrumcontrollerstub.h"

#include <QtMath>
#include <QDebug>

//! \brief Конструирует заглушку контроллера.
SpectrumControllerStub::SpectrumControllerStub(QObject *parent)
    : QObject(parent)
{
}

/*!
 *  \brief Генерирует и отправляет синтетический спектр.
 *  \param[in] viewMinHz Нижняя граница обзора, Гц.
 *  \param[in] viewMaxHz Верхняя граница обзора, Гц.
 */
void SpectrumControllerStub::requestSpectrum(double viewMinHz, double viewMaxHz)
{
    float minDb = -120.0f;
    float maxDb = 0.0f;
    QVariantList samples = generateSpectrum(viewMinHz, viewMaxHz, minDb, maxDb);
    emit spectrumReady(viewMinHz, viewMaxHz, samples, minDb, maxDb);
}

/*!
 *  \brief Логирует изменения полосы и отправляет echo-сигнал.
 *  \param[in] bandId Идентификатор полосы.
 *  \param[in] centerHz Центральная частота, Гц.
 *  \param[in] widthHz Ширина полосы, Гц.
 *  \param[in] isFinal Признак финального подтверждения изменения.
 */
void SpectrumControllerStub::setBand(int bandId, double centerHz, double widthHz, bool isFinal)
{
    qInfo().noquote()
        << QStringLiteral("setBand id=%1 centerHz=%2 widthHz=%3 final=%4")
               .arg(bandId)
               .arg(centerHz, 0, 'f', 2)
               .arg(widthHz, 0, 'f', 2)
               .arg(isFinal);
    emit bandStateChanged(bandId, centerHz, widthHz, 0.0, true);
}

/*!
 *  \brief Логирует изменение порога и отправляет echo-сигнал.
 *  \param[in] bandId Идентификатор полосы.
 *  \param[in] thresholdDb Порог, дБ.
 *  \param[in] isFinal Признак финального подтверждения изменения.
 */
void SpectrumControllerStub::setBandThreshold(int bandId, double thresholdDb, bool isFinal)
{
    qInfo().noquote()
        << QStringLiteral("setBandThreshold id=%1 thresholdDb=%2 final=%3")
               .arg(bandId)
               .arg(thresholdDb, 0, 'f', 1)
               .arg(isFinal);
    emit bandStateChanged(bandId, 0.0, 0.0, thresholdDb, true);
}

/*!
 *  \brief Логирует включение полосы и отправляет echo-сигнал.
 *  \param[in] bandId Идентификатор полосы.
 *  \param[in] enabled Признак включения.
 */
void SpectrumControllerStub::setBandEnabled(int bandId, bool enabled)
{
    qInfo().noquote()
        << QStringLiteral("setBandEnabled id=%1 enabled=%2")
               .arg(bandId)
               .arg(enabled);
    emit bandStateChanged(bandId, 0.0, 0.0, 0.0, enabled);
}

/*!
 *  \brief Формирует синтетический спектр с пиками.
 *  \param[in] viewMinHz Нижняя граница обзора, Гц.
 *  \param[in] viewMaxHz Верхняя граница обзора, Гц.
 *  \param[out] outMinDb Минимальное значение, дБ.
 *  \param[out] outMaxDb Максимальное значение, дБ.
 *  \return Список значений спектра.
 */
QVariantList SpectrumControllerStub::generateSpectrum(double viewMinHz, double viewMaxHz,
                                                      float &outMinDb, float &outMaxDb) const
{
    const int sampleCount = 4096;
    const double spanHz = qMax(1.0, viewMaxHz - viewMinHz);

    const double peaksHz[] = {1.2e9, 3.6e9, 6.8e9, 12.3e9, 16.2e9};
    const double peakWidthsHz[] = {80e6, 120e6, 150e6, 200e6, 140e6};
    const double peakHeightsDb[] = {40.0, 32.0, 38.0, 45.0, 28.0};

    QVariantList samples;
    samples.reserve(sampleCount);

    outMinDb = 1e9f;
    outMaxDb = -1e9f;

    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / (sampleCount - 1);
        const double freqHz = viewMinHz + t * spanHz;

        double noise = -98.0
            + 6.0 * qSin(freqHz * 1e-7)
            + 4.0 * qSin(freqHz * 3e-8)
            + 2.5 * qSin(freqHz * 7e-8);

        double value = noise;
        for (int p = 0; p < 5; ++p) {
            const double dist = (freqHz - peaksHz[p]) / peakWidthsHz[p];
            if (qAbs(dist) < 6.0) {
                value += peakHeightsDb[p] * qExp(-0.5 * dist * dist);
            }
        }

        outMinDb = qMin(outMinDb, static_cast<float>(value));
        outMaxDb = qMax(outMaxDb, static_cast<float>(value));
        samples.append(value);
    }

    outMinDb = qMin(outMinDb, -120.0f);
    outMaxDb = qMax(outMaxDb, -5.0f);

    return samples;
}
