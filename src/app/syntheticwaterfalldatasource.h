#ifndef SYNTHETICWATERFALLDATASOURCE_H
#define SYNTHETICWATERFALLDATASOURCE_H

#include "waterfallstorage.h"

#include <QVector>

struct SyntheticBandRange
{
    double centerHz = 0.0;
    double widthHz = 0.0;
};

struct SyntheticEmitter
{
    double relativePosition = 0.5;
    double widthFraction = 0.08;
    double amplitude = 0.85;
    double directionBias = 0.0;
    double wobbleFraction = 0.0;
    double phaseOffset = 0.0;
    double directionPeriodSec = 0.0;
    double directionPhaseRad = 0.0;
};

struct SyntheticWaterfallSourceConfig
{
    int binCount = 1024;
    QVector<SyntheticEmitter> emitters;
    bool restrictToBands = true;
};

class SyntheticWaterfallDataSource
{
public:
    SyntheticWaterfallDataSource();
    explicit SyntheticWaterfallDataSource(SyntheticWaterfallSourceConfig config);

    const SyntheticWaterfallSourceConfig& config() const noexcept { return m_config; }
    void setConfig(SyntheticWaterfallSourceConfig config);

    WaterfallRow nextRow(qint64 utcMs,
                         double sourceMinHz,
                         double sourceMaxHz,
                         const QVector<SyntheticBandRange>& bands) const;

private:
    SyntheticWaterfallSourceConfig m_config;
};

#endif // SYNTHETICWATERFALLDATASOURCE_H
