#include "hardware/simulator/simulated_bco_payload_accounting.h"

#include "core/domain_models.h"

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

long double bytesPerBatchForTarget(const ThroughputTarget& target)
{
    if (target.targetBytesPerSecond == 0 || target.batchPeriod.count() <= 0) {
        return 0.0L;
    }

    return static_cast<long double>(target.targetBytesPerSecond)
        * static_cast<long double>(target.batchPeriod.count()) / 1000.0L;
}

std::size_t boundedFloor(long double value)
{
    if (value <= 0.0L || !std::isfinite(static_cast<double>(value))) {
        return 0;
    }

    const auto maxSize = static_cast<long double>(std::numeric_limits<std::size_t>::max());
    if (value >= maxSize) {
        return std::numeric_limits<std::size_t>::max();
    }

    return static_cast<std::size_t>(std::floor(value));
}

} // namespace

ThroughputTarget baselineRawThroughput60MbpsTarget()
{
    ThroughputTarget target;
    target.targetBytesPerSecond = 60'000'000;
    target.batchPeriod = std::chrono::milliseconds{10};
    target.mode = PayloadAccountingMode::RawBcoBytes;
    target.packetModel.packetHeaderBytes = 32;
    target.packetModel.sampleRecordBytes = 16;
    target.packetModel.packetFooterBytes = 0;
    target.packetModel.samplesPerPacket = 256;
    target.packetModel.alignmentBytes = 0;
    return target;
}

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

std::size_t samplesPerPacketForModel(const SimulatedBcoPacketModel& model)
{
    return safeSamplesPerPacket(model);
}

std::size_t packetsPerBatchForTarget(const ThroughputTarget& target)
{
    const auto bytesPerBatch = bytesPerBatchForTarget(target);
    if (bytesPerBatch <= 0.0L) {
        return 0;
    }

    const auto packetBytes = static_cast<long double>(rawBytesPerPacket(target.packetModel));
    auto packetCount = boundedFloor(bytesPerBatch / packetBytes);
    if (packetCount == 0) {
        packetCount = 1;
    }
    return packetCount;
}

std::size_t samplesPerBatchForPacketTarget(const ThroughputTarget& target)
{
    return saturatingMultiplySize(packetsPerBatchForTarget(target),
                                  samplesPerPacketForModel(target.packetModel));
}

std::size_t samplesPerBatchForTarget(const ThroughputTarget& target)
{
    switch (target.mode) {
    case PayloadAccountingMode::RawBcoBytes:
        return samplesPerBatchForPacketTarget(target);
    case PayloadAccountingMode::ParsedSignalSampleBytes:
        break;
    }

    const auto bytesPerBatch = bytesPerBatchForTarget(target);
    if (bytesPerBatch <= 0.0L) {
        return 0;
    }

    return boundedFloor(bytesPerBatch / static_cast<long double>(sizeof(core::SignalSample)));
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
