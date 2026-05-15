#ifndef WATERFALLROWRESAMPLER_H
#define WATERFALLROWRESAMPLER_H

#include <QVector>

#include <cstdint>

struct WaterfallRow;

class WaterfallRowResampler
{
public:
    static QVector<uint16_t> resample(const WaterfallRow& row,
                                      double targetMinHz,
                                      double targetMaxHz,
                                      int targetBins);
};

#endif // WATERFALLROWRESAMPLER_H
