#include "waterfallrowresampler.h"

#include "waterfallstorage.h"

#include <algorithm>
#include <cmath>

namespace {

uint16_t interpolateBins(const QVector<uint16_t>& bins, double sourceBin)
{
    if (bins.isEmpty()) {
        return 0;
    }

    const int lastIndex = bins.size() - 1;
    const int left = std::clamp(static_cast<int>(std::floor(sourceBin)), 0, lastIndex);
    const int right = std::clamp(static_cast<int>(std::ceil(sourceBin)), 0, lastIndex);
    const double frac = std::clamp(sourceBin - static_cast<double>(left), 0.0, 1.0);
    const double value = static_cast<double>(bins.at(left)) * (1.0 - frac)
        + static_cast<double>(bins.at(right)) * frac;
    return static_cast<uint16_t>(std::clamp(std::lround(value), 0L, 65535L));
}

} // namespace

QVector<uint16_t> WaterfallRowResampler::resample(const WaterfallRow& row,
                                                  double targetMinHz,
                                                  double targetMaxHz,
                                                  int targetBins)
{
    QVector<uint16_t> result(std::max(0, targetBins), uint16_t{0});
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
