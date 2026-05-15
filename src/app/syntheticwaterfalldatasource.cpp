#include "syntheticwaterfalldatasource.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kDirectionalStrength = 0.75;

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
    return static_cast<uint16_t>(std::clamp(normalized, 0.0, 1.0) * 65535.0);
}

WaterfallBeamBin splitByDirection(double amplitude, double directionBias)
{
    const double bias = std::clamp(directionBias, -1.0, 1.0);
    const double leftFactor = 1.0 - kDirectionalStrength * bias;
    const double rightFactor = 1.0 + kDirectionalStrength * bias;
    return WaterfallBeamBin{toAmplitude(amplitude * leftFactor),
                            toAmplitude(amplitude * rightFactor)};
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
        SyntheticEmitter{0.5, 0.035, 0.86, -0.70, 0.0, 0.0, 22.0, -1.57079632679489661923},
        SyntheticEmitter{0.5, 0.035, 0.82, 0.60, 0.0, 0.0, 28.0, -0.45},
        SyntheticEmitter{0.5, 0.035, 0.78, 0.0, 0.0, 0.0, 34.0, 0.85},
        SyntheticEmitter{0.5, 0.035, 0.74, -0.45, 0.0, 0.0, 39.0, 1.80},
        SyntheticEmitter{0.5, 0.035, 0.70, 0.45, 0.0, 0.0, 25.0, 2.70}
    };
    return config;
}

double directionBiasForEmitter(const SyntheticEmitter& emitter, qint64 utcMs)
{
    if (emitter.directionPeriodSec <= 0.0 || !std::isfinite(emitter.directionPeriodSec)) {
        return emitter.directionBias;
    }

    double timeInPeriodSec = std::fmod(static_cast<double>(utcMs) / 1000.0,
                                       emitter.directionPeriodSec);
    if (timeInPeriodSec < 0.0) {
        timeInPeriodSec += emitter.directionPeriodSec;
    }

    const double phase = kTwoPi * timeInPeriodSec / emitter.directionPeriodSec
        + emitter.directionPhaseRad;
    return std::sin(phase);
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
        const double directionBias = directionBiasForEmitter(emitter, utcMs);
        addBin(row.bins[i], splitByDirection(peak * emitter.amplitude, directionBias));
    }

    return row;
}
