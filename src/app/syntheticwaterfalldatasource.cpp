#include "syntheticwaterfalldatasource.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace {

double bandMin(const SyntheticBandRange& band)
{
    return band.centerHz - band.widthHz * 0.5;
}

double bandMax(const SyntheticBandRange& band)
{
    return band.centerHz + band.widthHz * 0.5;
}

int findBandIndex(double frequencyHz, const QVector<SyntheticBandRange>& bands)
{
    for (int i = 0; i < bands.size(); ++i) {
        const auto& band = bands.at(i);
        if (band.widthHz > 0.0 && frequencyHz >= bandMin(band) && frequencyHz <= bandMax(band)) {
            return i;
        }
    }
    return -1;
}

uint16_t toAmplitude(double normalized)
{
    return static_cast<uint16_t>(std::clamp(std::lround(std::clamp(normalized, 0.0, 1.0) * 65535.0),
                                            0L,
                                            65535L));
}

WaterfallBeamBin splitByDirection(double amplitude, double directionBias)
{
    const double bias = std::clamp(directionBias, -1.0, 1.0);
    const double skew = 0.45 * std::abs(bias);
    const double leftScale = bias < 0.0 ? 1.0 + skew : 1.0 - skew;
    const double rightScale = bias > 0.0 ? 1.0 + skew : 1.0 - skew;
    return WaterfallBeamBin{toAmplitude(amplitude * leftScale),
                            toAmplitude(amplitude * rightScale)};
}

void addBin(WaterfallBeamBin& destination, const WaterfallBeamBin& source)
{
    destination.left = static_cast<uint16_t>(
        std::min<int>(65535, static_cast<int>(destination.left) + source.left));
    destination.right = static_cast<uint16_t>(
        std::min<int>(65535, static_cast<int>(destination.right) + source.right));
}

SyntheticWaterfallSourceConfig defaultConfig()
{
    SyntheticWaterfallSourceConfig config;
    config.emitters = {
        SyntheticEmitter{0.5, 0.035, 0.86, -0.70, 0.0, 0.0},
        SyntheticEmitter{0.5, 0.035, 0.82, 0.60, 0.0, 0.0},
        SyntheticEmitter{0.5, 0.035, 0.78, 0.0, 0.0, 0.0},
        SyntheticEmitter{0.5, 0.035, 0.74, -0.45, 0.0, 0.0},
        SyntheticEmitter{0.5, 0.035, 0.70, 0.45, 0.0, 0.0}
    };
    return config;
}

double directionBiasForBand(int bandIndex)
{
    constexpr double kBiases[] = {-0.70, 0.60, 0.0, -0.45, 0.45};
    constexpr int kBiasCount = static_cast<int>(std::size(kBiases));
    if (bandIndex < 0) {
        return 0.0;
    }
    return kBiases[bandIndex % kBiasCount];
}

} // namespace

SyntheticWaterfallDataSource::SyntheticWaterfallDataSource()
    : m_config(defaultConfig())
{
}

SyntheticWaterfallDataSource::SyntheticWaterfallDataSource(SyntheticWaterfallSourceConfig config)
    : m_config(std::move(config))
{
    if (m_config.emitters.isEmpty()) {
        m_config.emitters = defaultConfig().emitters;
    }
}

void SyntheticWaterfallDataSource::setConfig(SyntheticWaterfallSourceConfig config)
{
    m_config = std::move(config);
}

WaterfallRow SyntheticWaterfallDataSource::nextRow(qint64 utcMs,
                                                   double sourceMinHz,
                                                   double sourceMaxHz,
                                                   const QVector<SyntheticBandRange>& bands) const
{
    const int binCount = std::max(0, m_config.binCount);
    WaterfallRow row;
    row.utcMs = utcMs;
    row.viewMinHz = sourceMinHz;
    row.viewMaxHz = sourceMaxHz;
    row.bins = QVector<WaterfallBeamBin>(binCount, WaterfallBeamBin{});

    const double spanHz = sourceMaxHz - sourceMinHz;
    if (binCount <= 0 || spanHz <= 0.0) {
        return row;
    }

    for (int i = 0; i < binCount; ++i) {
        const double ratio = binCount <= 1
            ? 0.0
            : static_cast<double>(i) / static_cast<double>(binCount - 1);
        const double frequencyHz = sourceMinHz + ratio * spanHz;
        const int bandIndex = findBandIndex(frequencyHz, bands);
        const SyntheticBandRange *band = bandIndex >= 0 ? &bands.at(bandIndex) : nullptr;

        if (m_config.restrictToBands && !band) {
            continue;
        }

        const SyntheticBandRange sourceBand = band ? *band : SyntheticBandRange{
            sourceMinHz + spanHz * 0.5,
            spanHz
        };
        const double minHz = bandMin(sourceBand);
        const double maxHz = bandMax(sourceBand);
        const double bandWidthHz = std::max(1.0, maxHz - minHz);

        const qsizetype emitterIndex = m_config.emitters.isEmpty()
            ? 0
            : std::min<qsizetype>(bandIndex < 0 ? 0 : bandIndex,
                                  m_config.emitters.size() - 1);
        const SyntheticEmitter emitter = m_config.emitters.isEmpty()
            ? SyntheticEmitter{}
            : m_config.emitters.at(emitterIndex);
        const double centerHz = band ? band->centerHz : minHz + 0.5 * bandWidthHz;
        const double widthHz = std::max(1.0, emitter.widthFraction * bandWidthHz);
        const double peak = qExp(-qPow((frequencyHz - centerHz) / widthHz, 2.0));
        const double directionBias = bandIndex >= 0 && bandIndex < m_config.emitters.size()
            ? emitter.directionBias
            : directionBiasForBand(bandIndex);
        addBin(row.bins[i], splitByDirection(peak * emitter.amplitude, directionBias));
    }

    return row;
}
