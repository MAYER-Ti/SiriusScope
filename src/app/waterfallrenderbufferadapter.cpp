#include "waterfallrenderbufferadapter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace siriusscope::app {
namespace {

uint16_t domainAmplitude(int amplitude)
{
    return static_cast<uint16_t>(
        std::clamp(amplitude,
                   0,
                   static_cast<int>(kWaterfallRenderAmplitudeMax)));
}

bool hasBeam(const processing::WaterfallCell& cell, int beamIndex)
{
    return beamIndex >= 0
        && static_cast<std::size_t>(beamIndex) < cell.beamPresent.size()
        && cell.beamPresent[static_cast<std::size_t>(beamIndex)];
}

int beamAmplitude(const processing::WaterfallCell& cell, int beamIndex)
{
    if (beamIndex < 0
        || static_cast<std::size_t>(beamIndex) >= cell.beamAmplitudes.size()) {
        return 0;
    }

    return cell.beamAmplitudes[static_cast<std::size_t>(beamIndex)];
}

int binForFrequency(double frequencyHz, double sourceMinHz, double sourceMaxHz, int binCount)
{
    if (binCount <= 0 || sourceMaxHz <= sourceMinHz || !std::isfinite(frequencyHz)) {
        return -1;
    }

    if (frequencyHz < sourceMinHz || frequencyHz > sourceMaxHz) {
        return -1;
    }

    if (binCount == 1) {
        return 0;
    }

    const double ratio = (frequencyHz - sourceMinHz) / (sourceMaxHz - sourceMinHz);
    return std::clamp(static_cast<int>(std::lround(ratio * (binCount - 1))),
                      0,
                      binCount - 1);
}

} // namespace

WaterfallRenderBufferAdapterResult WaterfallRenderBufferAdapter::adaptFrame(
    const processing::WaterfallFrame& frame,
    qint64 utcMs,
    double sourceMinHz,
    double sourceMaxHz,
    int binCount)
{
    WaterfallRenderBufferAdapterResult result;
    result.row.utcMs = utcMs;
    result.row.viewMinHz = sourceMinHz;
    result.row.viewMaxHz = sourceMaxHz;
    result.row.bins = QVector<WaterfallBeamBin>(std::max(0, binCount), WaterfallBeamBin{});

    std::uint64_t firstSampleIndex = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t lastSampleIndex = 0;
    bool hasSampleIndex = false;

    if (result.row.bins.isEmpty() || sourceMaxHz <= sourceMinHz) {
        return result;
    }

    for (const auto& sourceRow : frame.rows) {
        firstSampleIndex = std::min(firstSampleIndex, sourceRow.sampleIndexStart);
        lastSampleIndex = std::max(lastSampleIndex, sourceRow.sampleIndexEnd);
        hasSampleIndex = true;

        for (const auto& cell : sourceRow.cells) {
            if (cell.status != processing::WaterfallCellStatus::Valid) {
                continue;
            }

            const double centerHz =
                (static_cast<double>(cell.frequencyRange.minHz)
                 + static_cast<double>(cell.frequencyRange.maxHz)) * 0.5;
            const int targetBin = binForFrequency(centerHz,
                                                  sourceMinHz,
                                                  sourceMaxHz,
                                                  result.row.bins.size());
            if (targetBin < 0) {
                continue;
            }

            WaterfallBeamBin& destination = result.row.bins[targetBin];
            if (hasBeam(cell, 0)) {
                destination.left = std::max(destination.left,
                                            domainAmplitude(beamAmplitude(cell, 0)));
            }
            if (hasBeam(cell, 1)) {
                destination.right = std::max(destination.right,
                                             domainAmplitude(beamAmplitude(cell, 1)));
            }
            result.hasVisibleCells = result.hasVisibleCells
                || destination.left > 0
                || destination.right > 0;
        }
    }

    if (hasSampleIndex) {
        result.row.firstSampleIndex = firstSampleIndex;
        result.row.lastSampleIndex = lastSampleIndex;
    }

    return result;
}

} // namespace siriusscope::app
