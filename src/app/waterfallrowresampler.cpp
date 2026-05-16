#include "waterfallrowresampler.h"

#include "waterfallstorage.h"

#include <algorithm>
#include <cmath>

namespace {

uint16_t interpolateValue(uint16_t leftValue, uint16_t rightValue, double frac)
{
    const double value = static_cast<double>(leftValue) * (1.0 - frac)
        + static_cast<double>(rightValue) * frac;
    return static_cast<uint16_t>(
        std::clamp(std::lround(value), 0L, static_cast<long>(kWaterfallRenderAmplitudeMax)));
}

WaterfallBeamBin interpolateBins(const QVector<WaterfallBeamBin>& bins, double sourceBin)
{
    if (bins.isEmpty()) {
        return {};
    }

    const int lastIndex = bins.size() - 1;
    const int left = std::clamp(static_cast<int>(std::floor(sourceBin)), 0, lastIndex);
    const int right = std::clamp(static_cast<int>(std::ceil(sourceBin)), 0, lastIndex);
    const double frac = std::clamp(sourceBin - static_cast<double>(left), 0.0, 1.0);
    return WaterfallBeamBin{
        interpolateValue(bins.at(left).left, bins.at(right).left, frac),
        interpolateValue(bins.at(left).right, bins.at(right).right, frac)
    };
}

} // namespace

QVector<WaterfallBeamBin> WaterfallRowResampler::resample(const WaterfallRow& row,
                                                          double targetMinHz,
                                                          double targetMaxHz,
                                                          int targetBins)
{
    QVector<WaterfallBeamBin> result(std::max(0, targetBins), WaterfallBeamBin{});
    if (result.isEmpty()
        || row.bins.isEmpty()
        || row.viewMaxHz <= row.viewMinHz
        || targetMaxHz <= targetMinHz) {
        return result;
    }

    const double targetSpanHz = targetMaxHz - targetMinHz;
    const double sourceSpanHz = row.viewMaxHz - row.viewMinHz;
    const int sourceLastBin = row.bins.size() - 1;

    for (int dstBin = 0; dstBin < result.size(); ++dstBin) {
        const double targetRatio = result.size() <= 1
            ? 0.0
            : static_cast<double>(dstBin) / static_cast<double>(result.size() - 1);
        const double freqHz = targetMinHz + targetRatio * targetSpanHz;

        if (freqHz < row.viewMinHz || freqHz > row.viewMaxHz) {
            continue;
        }

        const double sourceRatio = (freqHz - row.viewMinHz) / sourceSpanHz;
        const double sourceBin = sourceRatio * static_cast<double>(sourceLastBin);
        result[dstBin] = interpolateBins(row.bins, sourceBin);
    }

    return result;
}
