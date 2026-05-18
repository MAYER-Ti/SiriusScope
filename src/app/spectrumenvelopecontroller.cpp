#include "spectrumenvelopecontroller.h"

#include "core/domain_constraints.h"

#include <algorithm>
#include <cmath>

namespace siriusscope::app {
namespace {

constexpr int kPeakSpreadRadius = 24;
constexpr qint64 kEnvelopeHoldMs = 250;
constexpr qint64 kInactiveRefreshMs = -1;
constexpr double kPi = 3.14159265358979323846;

SpectrumEnvelopeControllerConfig normalizeConfig(SpectrumEnvelopeControllerConfig config)
{
    config.binCount = std::max(1, config.binCount);
    config.decayIntervalMs = std::max(1, config.decayIntervalMs);
    config.decayPerSecond = std::max(0.0, config.decayPerSecond);
    return config;
}

bool validViewport(double minHz, double maxHz)
{
    return std::isfinite(minHz) && std::isfinite(maxHz) && maxHz > minHz;
}

double peakSpreadWeight(int offset)
{
    const double distance = static_cast<double>(std::abs(offset));
    return 0.5 * (1.0 + std::cos(kPi * distance / static_cast<double>(kPeakSpreadRadius + 1)));
}

bool nearlyEqual(double left, double right)
{
    return std::abs(left - right) <= 1.0e-6;
}

} // namespace

SpectrumEnvelopeController::SpectrumEnvelopeController(QObject* parent)
    : SpectrumEnvelopeController(SpectrumEnvelopeControllerConfig{}, parent)
{
}

SpectrumEnvelopeController::SpectrumEnvelopeController(SpectrumEnvelopeControllerConfig config,
                                                       QObject* parent)
    : QObject(parent)
    , m_config(normalizeConfig(config))
    , m_envelope(static_cast<std::size_t>(m_config.binCount), 0.0)
    , m_lastRefreshMs(static_cast<std::size_t>(m_config.binCount), kInactiveRefreshMs)
{
    m_decayTimer.setInterval(m_config.decayIntervalMs);
    m_decayTimer.setSingleShot(false);
    connect(&m_decayTimer, &QTimer::timeout, this, &SpectrumEnvelopeController::applyDecay);
    m_monotonicClock.start();
}

void SpectrumEnvelopeController::setViewport(double minHz, double maxHz)
{
    if (maxHz < minHz) {
        std::swap(minHz, maxHz);
    }
    if (!validViewport(minHz, maxHz)) {
        return;
    }

    if (m_viewMinHz == minHz && m_viewMaxHz == maxHz) {
        return;
    }

    m_viewMinHz = minHz;
    m_viewMaxHz = maxHz;
    clearEnvelope();
    publishEnvelope();
}

void SpectrumEnvelopeController::ingestBatch(const hardware::BcoSampleBatch& batch)
{
    if (batch.samples.empty() || !validViewport(m_viewMinHz, m_viewMaxHz)) {
        return;
    }

    std::vector<double> batchEnvelope(m_envelope.size(), 0.0);
    for (const auto& sample : batch.samples) {
        if (sample.amplitude < core::DomainConstraints::minAmplitude
            || sample.amplitude > core::DomainConstraints::maxAmplitude) {
            continue;
        }

        const auto bin = frequencyBinFor(sample.absoluteFrequencyHz);
        if (!bin) {
            continue;
        }

        const double amplitude = static_cast<double>(sample.amplitude);
        const int centerBin = static_cast<int>(*bin);
        const int binCount = static_cast<int>(m_envelope.size());
        for (int offset = -kPeakSpreadRadius; offset <= kPeakSpreadRadius; ++offset) {
            const int targetBin = centerBin + offset;
            if (targetBin < 0 || targetBin >= binCount) {
                continue;
            }

            auto& current = batchEnvelope.at(static_cast<std::size_t>(targetBin));
            const double spreadAmplitude = amplitude * peakSpreadWeight(offset);
            if (spreadAmplitude > current) {
                current = spreadAmplitude;
            }
        }
    }

    const qint64 nowMs = m_monotonicClock.elapsed();
    bool changed = false;
    bool refreshed = false;
    for (std::size_t index = 0; index < batchEnvelope.size(); ++index) {
        const double observed = batchEnvelope.at(index);
        if (observed <= 0.0) {
            continue;
        }

        refreshed = true;
        m_lastRefreshMs.at(index) = nowMs;
        auto& current = m_envelope.at(index);
        if (!nearlyEqual(current, observed)) {
            current = observed;
            changed = true;
        }
    }

    if (!refreshed) {
        return;
    }

    ensureDecayTimerRunning();
    if (!changed) {
        return;
    }

    publishEnvelope();
}

QVariantList SpectrumEnvelopeController::envelopeSamples() const
{
    return buildSamples();
}

void SpectrumEnvelopeController::applyDecay()
{
    if (m_config.decayPerSecond <= 0.0) {
        return;
    }
    if (!m_decayClock.isValid()) {
        m_decayClock.start();
        return;
    }

    const qint64 elapsedMs = m_decayClock.restart();
    if (elapsedMs <= 0) {
        return;
    }

    const double decrement = m_config.decayPerSecond * static_cast<double>(elapsedMs) / 1000.0;
    const qint64 nowMs = m_monotonicClock.elapsed();
    bool changed = false;
    bool hasSignal = false;
    for (std::size_t index = 0; index < m_envelope.size(); ++index) {
        auto& value = m_envelope.at(index);
        if (value <= 0.0) {
            continue;
        }

        const qint64 lastRefreshMs = m_lastRefreshMs.at(index);
        if (lastRefreshMs != kInactiveRefreshMs && nowMs - lastRefreshMs <= kEnvelopeHoldMs) {
            hasSignal = true;
            continue;
        }

        value = value <= decrement ? 0.0 : value - decrement;
        if (value <= 0.0) {
            m_lastRefreshMs.at(index) = kInactiveRefreshMs;
        }
        changed = true;
        hasSignal = hasSignal || value > 0.0;
    }

    if (changed) {
        publishEnvelope();
    }
    if (!hasSignal) {
        m_decayTimer.stop();
        m_decayClock.invalidate();
    }
}

std::optional<std::size_t> SpectrumEnvelopeController::frequencyBinFor(
    std::int64_t frequencyHz) const
{
    if (!validViewport(m_viewMinHz, m_viewMaxHz)
        || frequencyHz < m_viewMinHz
        || frequencyHz > m_viewMaxHz) {
        return std::nullopt;
    }

    const double spanHz = m_viewMaxHz - m_viewMinHz;
    const double ratio = (static_cast<double>(frequencyHz) - m_viewMinHz) / spanHz;
    auto bin = static_cast<std::size_t>(std::floor(ratio * static_cast<double>(m_config.binCount)));
    if (bin >= m_envelope.size()) {
        bin = m_envelope.size() - 1;
    }

    return bin;
}

QVariantList SpectrumEnvelopeController::buildSamples() const
{
    QVariantList samples;
    samples.reserve(static_cast<qsizetype>(m_envelope.size()));
    for (const auto value : m_envelope) {
        samples.append(value);
    }
    return samples;
}

void SpectrumEnvelopeController::publishEnvelope()
{
    emit envelopeChanged(m_viewMinHz, m_viewMaxHz, buildSamples());
}

void SpectrumEnvelopeController::clearEnvelope()
{
    std::fill(m_envelope.begin(), m_envelope.end(), 0.0);
    std::fill(m_lastRefreshMs.begin(), m_lastRefreshMs.end(), kInactiveRefreshMs);
    m_decayTimer.stop();
    m_decayClock.invalidate();
}

void SpectrumEnvelopeController::ensureDecayTimerRunning()
{
    if (m_config.decayPerSecond <= 0.0) {
        return;
    }
    if (m_decayTimer.isActive()) {
        return;
    }

    m_decayClock.restart();
    m_decayTimer.start();
}

} // namespace siriusscope::app
