#pragma once

#include "core/domain_models.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::processing {

struct SpectrumEnvelopeProcessorConfig
{
    int binCount = 1024;
    double decayPerSecond = 80.0;
    int peakSpreadRadius = 8;
    std::int64_t holdMs = 250;
};

class SpectrumEnvelopeProcessor
{
public:
    explicit SpectrumEnvelopeProcessor(SpectrumEnvelopeProcessorConfig config = {});

    void setViewport(double minHz, double maxHz);
    bool ingestSamples(const std::vector<core::SignalSample>& samples, std::int64_t nowMs);
    bool applyDecay(std::int64_t elapsedMs, std::int64_t nowMs);

    bool hasActiveSignal() const noexcept;
    bool viewportValid() const noexcept;
    double viewMinHz() const noexcept { return m_viewMinHz; }
    double viewMaxHz() const noexcept { return m_viewMaxHz; }
    const std::vector<float>& samples() const noexcept { return m_samples; }

private:
    std::optional<std::size_t> frequencyBinFor(std::int64_t frequencyHz) const;
    void clearEnvelope();
    void rebuildSpreadWeights();

    SpectrumEnvelopeProcessorConfig m_config;
    std::vector<double> m_envelope;
    std::vector<float> m_samples;
    std::vector<std::int64_t> m_lastRefreshMs;
    std::vector<double> m_batchEnvelope;
    std::vector<unsigned char> m_batchTouchedFlags;
    std::vector<std::size_t> m_touchedBins;
    std::vector<double> m_spreadWeights;
    double m_viewMinHz = 0.0;
    double m_viewMaxHz = 0.0;
};

} // namespace siriusscope::processing
