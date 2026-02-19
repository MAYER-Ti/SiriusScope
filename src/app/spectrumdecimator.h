/*! \file spectrumdecimator.h
 *  \brief Помощник для декомации спектра при отрисовке.
 */
#ifndef SPECTRUMDECIMATOR_H
#define SPECTRUMDECIMATOR_H

#include <QObject>
#include <QVariantList>

/*! \class SpectrumDecimator
 *  \brief Сводит спектр к парам min/max на колонку пикселей.
 */
class SpectrumDecimator : public QObject
{
    Q_OBJECT
public:
    //! \brief Конструирует декоматор.
    explicit SpectrumDecimator(QObject *parent = nullptr);

    /*! \brief Возвращает список min/max пар для целевой ширины.
     *  \param[in] samples Исходные значения спектра.
     *  \param[in] targetWidth Целевая ширина в пикселях.
     *  \return Список значений вида [min0, max0, min1, max1, ...].
     */
    Q_INVOKABLE QVariantList decimateMinMax(const QVariantList &samples, int targetWidth) const;
};

#endif // SPECTRUMDECIMATOR_H
