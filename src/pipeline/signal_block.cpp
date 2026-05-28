#include "pipeline/signal_block.h"

#include <algorithm>

namespace siriusscope::pipeline {

SignalBlock::SignalBlock(std::size_t maxSamples)
    : m_maxSamples(maxSamples)
{
    m_samples.reserve(m_maxSamples);
}

void SignalBlock::reset(SignalBlockMetadata metadata)
{
    if (metadata.producedAt == std::chrono::steady_clock::time_point{}) {
        metadata.producedAt = std::chrono::steady_clock::now();
    }

    m_metadata = metadata;
    m_samples.clear();
}

bool SignalBlock::assignSamples(std::span<const core::SignalSample> samples)
{
    if (samples.size() > m_maxSamples) {
        return false;
    }

    m_samples.assign(samples.begin(), samples.end());
    if (!m_samples.empty()) {
        if (m_metadata.firstSampleIndex == 0 && m_metadata.lastSampleIndex == 0) {
            m_metadata.firstSampleIndex = m_samples.front().sampleIndex;
            m_metadata.lastSampleIndex = m_samples.back().sampleIndex;
        } else {
            m_metadata.firstSampleIndex =
                std::min(m_metadata.firstSampleIndex, m_samples.front().sampleIndex);
            m_metadata.lastSampleIndex =
                std::max(m_metadata.lastSampleIndex, m_samples.back().sampleIndex);
        }
    }
    return true;
}

std::span<const core::SignalSample> SignalBlock::samples() const noexcept
{
    return m_samples;
}

std::span<core::SignalSample> SignalBlock::mutableSamples() noexcept
{
    return m_samples;
}

} // namespace siriusscope::pipeline
