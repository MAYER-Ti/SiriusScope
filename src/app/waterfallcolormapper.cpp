#include "waterfallcolormapper.h"

#include <algorithm>
#include <cmath>

namespace {

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

uint8_t toByte(double value)
{
    return static_cast<uint8_t>(clamp01(value) * 255.0 + 0.5);
}

uint8_t alphaForBrightness(double brightness)
{
    if (brightness <= 0.0) {
        return 0;
    }
    return toByte(std::clamp(0.20 + brightness * 0.80, 0.0, 1.0));
}

double normalizedBrightness(const WaterfallBeamBin& bin, const WaterfallColorParams& params)
{
    const uint16_t amplitude = std::max(bin.left, bin.right);
    const double minAmplitude = static_cast<double>(params.amplitudeMin);
    const double maxAmplitude = static_cast<double>(params.amplitudeMax);
    if (maxAmplitude <= minAmplitude) {
        return amplitude > params.amplitudeMin ? 1.0 : 0.0;
    }

    const double normalized = clamp01((static_cast<double>(amplitude) - minAmplitude)
                                      / (maxAmplitude - minAmplitude));
    return std::pow(normalized, std::max(0.01, params.gamma));
}

double directionValue(const WaterfallBeamBin& bin, const WaterfallColorParams& params)
{
    constexpr double kEpsilon = 1.0e-9;
    const double left = static_cast<double>(bin.left);
    const double right = static_cast<double>(bin.right);
    const double raw = (right - left) / (right + left + kEpsilon);
    const double deadZone = clamp01(params.directionDeadZone);
    const double absRaw = std::abs(raw);

    if (absRaw < deadZone || deadZone >= 1.0) {
        return 0.0;
    }

    const double scaled = (absRaw - deadZone) / (1.0 - deadZone);
    return std::copysign(clamp01(scaled), raw);
}

} // namespace

Rgba8 WaterfallColorMapper::map(const WaterfallBeamBin& bin, const WaterfallColorParams& params)
{
    const double brightness = normalizedBrightness(bin, params);
    double r = brightness;
    double g = brightness;
    double b = brightness;

    if (params.directionalEnabled && brightness > 0.0) {
        const double direction = directionValue(bin, params);
        const double mixFactor = clamp01(std::abs(direction) * params.directionalAlpha);

        if (direction < 0.0) {
            const double targetR = brightness;
            const double targetG = brightness * 0.18;
            const double targetB = brightness * 0.12;
            r = r * (1.0 - mixFactor) + targetR * mixFactor;
            g = g * (1.0 - mixFactor) + targetG * mixFactor;
            b = b * (1.0 - mixFactor) + targetB * mixFactor;
        } else if (direction > 0.0) {
            const double targetR = brightness * 0.12;
            const double targetG = brightness;
            const double targetB = brightness * 0.18;
            r = r * (1.0 - mixFactor) + targetR * mixFactor;
            g = g * (1.0 - mixFactor) + targetG * mixFactor;
            b = b * (1.0 - mixFactor) + targetB * mixFactor;
        }
    }

    return Rgba8{toByte(r), toByte(g), toByte(b), alphaForBrightness(brightness)};
}
