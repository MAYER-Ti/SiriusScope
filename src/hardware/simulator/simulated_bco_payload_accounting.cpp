#include "hardware/simulator/simulated_bco_payload_accounting.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace siriusscope::hardware {

namespace {

std::size_t safeSamplesPerPacket(const SimulatedBcoPacketModel& model)
{
    return std::max<std::size_t>(1, model.samplesPerPacket);
}

std::size_t safeSampleRecordBytes(const SimulatedBcoPacketModel& model)
{
    return std::max<std::size_t>(1, model.sampleRecordBytes);
}

std::size_t saturatingAddSize(std::size_t value, std::size_t increment)
{
    if (increment > std::numeric_limits<std::size_t>::max() - value) {
        return std::numeric_limits<std::size_t>::max();
    }
    return value + increment;
}

std::size_t saturatingMultiplySize(std::size_t left, std::size_t right)
{
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return std::numeric_limits<std::size_t>::max();
    }
    return left * right;
}

std::uint64_t saturatingMultiply64(std::uint64_t left, std::uint64_t right)
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

std::uint64_t ceilDivide(std::uint64_t value, std::uint64_t divisor)
{
    if (divisor == 0) {
        return value;
    }
    return value / divisor + (value % divisor == 0 ? 0 : 1);
}

} // namespace

std::size_t rawBytesPerPacket(const SimulatedBcoPacketModel& model)
{
    const auto sampleBytes =
        saturatingMultiplySize(safeSamplesPerPacket(model), safeSampleRecordBytes(model));

    auto packetBytes = saturatingAddSize(model.packetHeaderBytes, model.packetFooterBytes);
    packetBytes = saturatingAddSize(packetBytes, sampleBytes);
    packetBytes = saturatingAddSize(packetBytes, model.alignmentBytes);
    return std::max<std::size_t>(1, packetBytes);
}

double rawBytesPerSample(const SimulatedBcoPacketModel& model)
{
    return static_cast<double>(rawBytesPerPacket(model))
        / static_cast<double>(safeSamplesPerPacket(model));
}

std::size_t samplesPerBatchForTarget(const ThroughputTarget& target)
{
    const auto safeTargetBytesPerSecond =
        std::max<std::uint64_t>(1, target.targetBytesPerSecond);
    const auto safeBatchPeriodMs =
        std::max<std::int64_t>(1, target.batchPeriod.count());
    const long double bytesPerBatch =
        static_cast<long double>(safeTargetBytesPerSecond)
        * static_cast<long double>(safeBatchPeriodMs) / 1000.0L;
    const long double bytesPerSample =
        static_cast<long double>(rawBytesPerSample(target.packetModel));
    if (bytesPerSample <= 0.0L || !std::isfinite(static_cast<double>(bytesPerSample))) {
        return 0;
    }

    const auto samples = std::floor(bytesPerBatch / bytesPerSample);
    if (samples <= 0.0L) {
        return 0;
    }
    const auto maxSamples = static_cast<long double>(std::numeric_limits<std::size_t>::max());
    if (samples >= maxSamples) {
        return std::numeric_limits<std::size_t>::max();
    }

    return static_cast<std::size_t>(samples);
}

std::uint64_t rawBytesForSamples(std::size_t sampleCount,
                                 const SimulatedBcoPacketModel& model)
{
    if (sampleCount == 0) {
        return 0;
    }

    const auto safePacketSampleCount =
        static_cast<std::uint64_t>(safeSamplesPerPacket(model));
    const auto packetCount =
        ceilDivide(static_cast<std::uint64_t>(sampleCount), safePacketSampleCount);
    const auto packetBytes = static_cast<std::uint64_t>(rawBytesPerPacket(model));
    return saturatingMultiply64(packetCount, packetBytes);
}

} // namespace siriusscope::hardware
