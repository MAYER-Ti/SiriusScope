#ifndef FREQUENCYGRIDMODEL_H
#define FREQUENCYGRIDMODEL_H

#include <QObject>
#include <QVariantList>

class FrequencyGridModel : public QObject
{
    Q_OBJECT

public:
    explicit FrequencyGridModel(QObject *parent = nullptr);

    Q_INVOKABLE QVariantList buildTicks(double viewMinHz,
                                        double viewMaxHz,
                                        int pixelWidth) const;
};

#endif // FREQUENCYGRIDMODEL_H
