#include "frequencygridmodel.h"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kTargetMajorSpacingPx = 120.0;
constexpr double kMinStepHz = 1.0;

double niceStep(double rawStep)
{
    if (!std::isfinite(rawStep) || rawStep <= 0.0) {
        return kMinStepHz;
    }

    const double exponent = std::floor(std::log10(rawStep));
    const double base = std::pow(10.0, exponent);
    const double fraction = rawStep / base;

    double niceFraction = 10.0;
    if (fraction <= 1.0) {
        niceFraction = 1.0;
    } else if (fraction <= 2.0) {
        niceFraction = 2.0;
    } else if (fraction <= 5.0) {
        niceFraction = 5.0;
    }

    return std::max(kMinStepHz, niceFraction * base);
}

QString formatHz(double valueHz)
{
    const double absHz = std::abs(valueHz);
    if (absHz >= 1e9) {
        return QStringLiteral("%1 GHz").arg(valueHz / 1e9, 0, 'f', 2);
    }
    if (absHz >= 1e6) {
        return QStringLiteral("%1 MHz").arg(valueHz / 1e6, 0, 'f', 0);
    }
    if (absHz >= 1e3) {
        return QStringLiteral("%1 kHz").arg(valueHz / 1e3, 0, 'f', 0);
    }
    return QStringLiteral("%1 Hz").arg(valueHz, 0, 'f', 0);
}

QVariantMap makeTick(double frequencyHz)
{
    QVariantMap tick;
    tick.insert(QStringLiteral("frequencyHz"), frequencyHz);
    tick.insert(QStringLiteral("label"), formatHz(frequencyHz));
    tick.insert(QStringLiteral("major"), true);
    return tick;
}

} // namespace

FrequencyGridModel::FrequencyGridModel(QObject *parent)
    : QObject(parent)
{
}

QVariantList FrequencyGridModel::buildTicks(double viewMinHz,
                                            double viewMaxHz,
                                            int pixelWidth) const
{
    QVariantList ticks;
    if (!std::isfinite(viewMinHz) || !std::isfinite(viewMaxHz) || pixelWidth <= 0) {
        return ticks;
    }
    if (viewMaxHz < viewMinHz) {
        std::swap(viewMinHz, viewMaxHz);
    }

    const double spanHz = viewMaxHz - viewMinHz;
    if (spanHz <= 0.0) {
        return ticks;
    }

    const int targetTickCount =
        std::clamp(static_cast<int>(std::round(pixelWidth / kTargetMajorSpacingPx)) + 1, 2, 12);
    const double stepHz = niceStep(spanHz / std::max(1, targetTickCount - 1));
    const double firstTickHz = std::ceil(viewMinHz / stepHz) * stepHz;

    ticks.push_back(makeTick(viewMinHz));

    const double epsilonHz = std::max(1.0, stepHz * 1e-6);
    for (double frequencyHz = firstTickHz;
         frequencyHz <= viewMaxHz + epsilonHz && ticks.size() < 32;
         frequencyHz += stepHz) {
        if (frequencyHz <= viewMinHz + epsilonHz || frequencyHz >= viewMaxHz - epsilonHz) {
            continue;
        }
        ticks.push_back(makeTick(frequencyHz));
    }

    ticks.push_back(makeTick(viewMaxHz));
    return ticks;
}
