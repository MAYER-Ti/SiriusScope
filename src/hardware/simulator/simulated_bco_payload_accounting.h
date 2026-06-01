#pragma once

#include "hardware/hardware_profile.h"

#include <cstddef>
#include <cstdint>

namespace siriusscope::hardware {

std::size_t rawBytesPerPacket(const SimulatedBcoPacketModel& model);
double rawBytesPerSample(const SimulatedBcoPacketModel& model);
std::size_t samplesPerPacketForModel(const SimulatedBcoPacketModel& model);
std::size_t packetsPerBatchForTarget(const ThroughputTarget& target);
std::size_t samplesPerBatchForPacketTarget(const ThroughputTarget& target);
std::size_t samplesPerBatchForTarget(const ThroughputTarget& target);
std::uint64_t rawBytesForSamples(std::size_t sampleCount,
                                 const SimulatedBcoPacketModel& model);

} // namespace siriusscope::hardware
