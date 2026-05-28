#pragma once

#include "core/domain_models.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace siriusscope::pipeline {

struct SignalBlockMetadata
{
    std::uint64_t sequenceId = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
    std::chrono::steady_clock::time_point producedAt{};
};

class SignalBlock
{
public:
    explicit SignalBlock(std::size_t maxSamples = 0);

    void reset(SignalBlockMetadata metadata = {});
    bool assignSamples(std::span<const core::SignalSample> samples);

    std::uint64_t sequenceId() const noexcept { return m_metadata.sequenceId; }
    std::uint64_t firstSampleIndex() const noexcept { return m_metadata.firstSampleIndex; }
    std::uint64_t lastSampleIndex() const noexcept { return m_metadata.lastSampleIndex; }
    std::chrono::steady_clock::time_point producedAt() const noexcept
    {
        return m_metadata.producedAt;
    }

    std::size_t sampleCount() const noexcept { return m_samples.size(); }
    std::size_t maxSamples() const noexcept { return m_maxSamples; }
    bool empty() const noexcept { return m_samples.empty(); }

    std::span<const core::SignalSample> samples() const noexcept;
    std::span<core::SignalSample> mutableSamples() noexcept;

private:
    SignalBlockMetadata m_metadata;
    std::size_t m_maxSamples = 0;
    std::vector<core::SignalSample> m_samples;
};

} // namespace siriusscope::pipeline
