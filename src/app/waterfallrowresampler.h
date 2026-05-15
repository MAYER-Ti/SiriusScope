#ifndef WATERFALLROWRESAMPLER_H
#define WATERFALLROWRESAMPLER_H

#include <QVector>

#include <cstdint>

struct WaterfallBeamBin;
struct WaterfallRow;

class WaterfallRowResampler
{
public:
    static QVector<WaterfallBeamBin> resample(const WaterfallRow& row,
                                              double targetMinHz,
                                              double targetMaxHz,
                                              int targetBins);
};

#endif // WATERFALLROWRESAMPLER_H
