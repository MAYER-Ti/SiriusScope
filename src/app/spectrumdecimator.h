#ifndef SPECTRUMDECIMATOR_H
#define SPECTRUMDECIMATOR_H

#include <QObject>
#include <QVariantList>

class SpectrumDecimator : public QObject
{
    Q_OBJECT
public:
    explicit SpectrumDecimator(QObject *parent = nullptr);

    Q_INVOKABLE QVariantList decimateMinMax(const QVariantList &samples, int targetWidth) const;
};

#endif // SPECTRUMDECIMATOR_H
