#ifndef WATERFALLCOLORMAPPER_H
#define WATERFALLCOLORMAPPER_H

#include "waterfallstorage.h"

#include <cstdint>

struct WaterfallColorParams
{
    uint16_t amplitudeMin = kWaterfallRenderAmplitudeMin;
    uint16_t amplitudeMax = kWaterfallRenderAmplitudeMax;
    // Render threshold only; domain amplitude 1 remains valid input data.
    uint16_t displayAmplitudeThreshold = 4;
    double gamma = 0.7;
    double directionDeadZone = 0.10;
    double directionalAlpha = 0.35;
    bool directionalEnabled = true;
};

struct Rgba8
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;
};

class WaterfallColorMapper
{
public:
    static Rgba8 map(const WaterfallBeamBin& bin, const WaterfallColorParams& params);
};

#endif // WATERFALLCOLORMAPPER_H
