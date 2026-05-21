#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace siriusscope::hardware {

struct BcoStreamConfig
{
    std::vector<core::BandConfig> bandConfigs;
    core::TimeBase timeBase;
    std::uint64_t sessionId = 0;
};

struct BcoBatchStats
{
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t lastSampleIndex = 0;
    std::uint64_t sampleCount = 0;
    std::uint64_t packetCount = 0;
    std::uint64_t lostPacketCount = 0;
    std::uint64_t malformedPacketCount = 0;
    std::chrono::steady_clock::time_point producedAt{};
};

struct BcoSourceMetrics
{
    std::uint64_t producedSamples = 0;
    std::uint64_t producedBatches = 0;
    std::uint64_t droppedSamples = 0;
    std::uint64_t droppedBatches = 0;
    std::uint64_t lostPackets = 0;
    std::uint64_t malformedPackets = 0;
    double producedSamplesPerSecond = 0.0;
    double equivalentMegabytesPerSecond = 0.0;
    std::chrono::milliseconds maxCallbackDuration{0};
};

struct BcoSampleBlock
{
    std::vector<core::SignalSample> samples;
    BcoBatchStats stats;
};

class IBcoStreamSource
{
public:
    using SampleBlockPtr = std::shared_ptr<const BcoSampleBlock>;
    using SampleBlockCallback = std::function<void(SampleBlockPtr)>;

    virtual ~IBcoStreamSource() = default;

    virtual core::OperationResult configure(const BcoStreamConfig& config) = 0;
    virtual core::OperationResult start(SampleBlockCallback callback) = 0;
    virtual core::OperationResult stop() = 0;
    virtual BcoSourceMetrics metrics() const = 0;
};

} // namespace siriusscope::hardware
