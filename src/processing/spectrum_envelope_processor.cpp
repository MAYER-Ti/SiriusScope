#include "spectrum_envelope_processor.h"

#include "core/domain_constraints.h"

#include <algorithm>
#include <cmath>

namespace siriusscope::processing {
namespace {

constexpr std::int64_t kInactiveRefreshMs = -1;
constexpr double kPi = 3.14159265358979323846;

SpectrumEnvelopeProcessorConfig normalizeConfig(SpectrumEnvelopeProcessorConfig config)
{
    config.binCount = std::max(1, config.binCount);
    config.decayPerSecond = std::max(0.0, config.decayPerSecond);
    config.peakSpreadRadius = std::max(0, config.peakSpreadRadius);
    config.holdMs = std::max<std::int64_t>(0, config.holdMs);
    return config;
}

bool validViewport(double minHz, double maxHz)
{
    return std::isfinite(minHz) && std::isfinite(maxHz) && maxHz > minHz;
}

bool nearlyEqual(double left, double right)
{
    return std::abs(left - right) <= 1.0e-6;
}

} // namespace

SpectrumEnvelopeProcessor::SpectrumEnvelopeProcessor(SpectrumEnvelopeProcessorConfig config)
    : m_config(normalizeConfig(config))
    , m_envelope(static_cast<std::size_t>(m_config.binCount), 0.0)
    , m_samples(static_cast<std::size_t>(m_config.binCount), 0.0F)
    , m_lastRefreshMs(static_cast<std::size_t>(m_config.binCount), kInactiveRefreshMs)
    , m_batchEnvelope(static_cast<std::size_t>(m_config.binCount), 0.0)
    , m_batchTouchedFlags(static_cast<std::size_t>(m_config.binCount), 0)
{
    m_touchedBins.reserve(static_cast<std::size_t>(m_config.peakSpreadRadius * 2 + 1));
    rebuildSpreadWeights();
}

void SpectrumEnvelopeProcessor::setViewport(double minHz, double maxHz)
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
}

void SpectrumEnvelopeProcessor::reset()
{
    clearEnvelope();
}

bool SpectrumEnvelopeProcessor::ingestSamples(const std::vector<core::SignalSample>& samples,
                                              std::int64_t nowMs)
{
    if (samples.empty() || !viewportValid()) {
        return false;
    }

    for (const auto index : m_touchedBins) {
        m_batchEnvelope[index] = 0.0;
        m_batchTouchedFlags[index] = 0;
    }
    m_touchedBins.clear();

    const int binCount = static_cast<int>(m_envelope.size());
    for (const auto& sample : samples) {
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
        for (int offset = -m_config.peakSpreadRadius; offset <= m_config.peakSpreadRadius;
             ++offset) {
            const int targetBin = centerBin + offset;
            if (targetBin < 0 || targetBin >= binCount) {
                continue;
            }

            const auto targetIndex = static_cast<std::size_t>(targetBin);
            if (m_batchTouchedFlags[targetIndex] == 0) {
                m_batchTouchedFlags[targetIndex] = 1;
                m_touchedBins.push_back(targetIndex);
            }

            auto& current = m_batchEnvelope[targetIndex];
            const auto weightIndex = static_cast<std::size_t>(offset + m_config.peakSpreadRadius);
            const double spreadAmplitude = amplitude * m_spreadWeights[weightIndex];
            if (spreadAmplitude > current) {
                current = spreadAmplitude;
            }
        }
    }

    bool changed = false;
    for (const auto index : m_touchedBins) {
        const double observed = m_batchEnvelope[index];
        if (observed <= 0.0) {
            continue;
        }

        m_lastRefreshMs[index] = nowMs;
        auto& current = m_envelope[index];
        if (!nearlyEqual(current, observed)) {
            current = observed;
            m_samples[index] = static_cast<float>(observed);
            changed = true;
        }
    }

    return changed;
}

bool SpectrumEnvelopeProcessor::applyDecay(std::int64_t elapsedMs, std::int64_t nowMs)
{
    if (m_config.decayPerSecond <= 0.0 || elapsedMs <= 0) {
        return false;
    }

    const double decrement = m_config.decayPerSecond * static_cast<double>(elapsedMs) / 1000.0;
    bool changed = false;
    for (std::size_t index = 0; index < m_envelope.size(); ++index) {
        auto& value = m_envelope[index];
        if (value <= 0.0) {
            continue;
        }

        const std::int64_t lastRefreshMs = m_lastRefreshMs[index];
        if (lastRefreshMs != kInactiveRefreshMs && nowMs - lastRefreshMs <= m_config.holdMs) {
            continue;
        }

        value = value <= decrement ? 0.0 : value - decrement;
        if (value <= 0.0) {
            m_lastRefreshMs[index] = kInactiveRefreshMs;
        }
        m_samples[index] = static_cast<float>(value);
        changed = true;
    }

    return changed;
}

bool SpectrumEnvelopeProcessor::hasActiveSignal() const noexcept
{
    return std::any_of(m_envelope.cbegin(), m_envelope.cend(), [](double value) {
        return value > 0.0;
    });
}

bool SpectrumEnvelopeProcessor::viewportValid() const noexcept
{
    return validViewport(m_viewMinHz, m_viewMaxHz);
}

std::optional<std::size_t> SpectrumEnvelopeProcessor::frequencyBinFor(
    std::int64_t frequencyHz) const
{
    if (!viewportValid() || frequencyHz < m_viewMinHz || frequencyHz > m_viewMaxHz) {
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

void SpectrumEnvelopeProcessor::clearEnvelope()
{
    std::fill(m_envelope.begin(), m_envelope.end(), 0.0);
    std::fill(m_samples.begin(), m_samples.end(), 0.0F);
    std::fill(m_lastRefreshMs.begin(), m_lastRefreshMs.end(), kInactiveRefreshMs);
    for (const auto index : m_touchedBins) {
        m_batchEnvelope[index] = 0.0;
        m_batchTouchedFlags[index] = 0;
    }
    m_touchedBins.clear();
}

void SpectrumEnvelopeProcessor::rebuildSpreadWeights()
{
    const int radius = m_config.peakSpreadRadius;
    m_spreadWeights.resize(static_cast<std::size_t>(radius * 2 + 1));
    for (int offset = -radius; offset <= radius; ++offset) {
        const double distance = static_cast<double>(std::abs(offset));
        const double weight = 0.5 * (1.0 + std::cos(kPi * distance / static_cast<double>(radius + 1)));
        m_spreadWeights[static_cast<std::size_t>(offset + radius)] = weight;
    }
}

} // namespace siriusscope::processing
