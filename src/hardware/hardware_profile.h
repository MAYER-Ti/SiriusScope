#pragma once

#include "hardware/interfaces/bco_stream_source.h"
#include "hardware/simulator/simulator_pulse_config.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::hardware {

enum class DataSourceMode
{
    Simulator,
    RealHardware,
};

enum class SimulatorLoadProfile
{
    UiDemo,
    MediumLoad,
    RealBcoEquivalent,
    Stress150Percent,
    BaselineRawThroughput60MBps,
    TargetRawThroughput90MBps,
};

enum class PayloadAccountingMode
{
    RawBcoBytes,
    ParsedSignalSampleBytes,
};

struct SimulatedBcoPacketModel
{
    std::size_t packetHeaderBytes = 32;
    std::size_t sampleRecordBytes = 16;
    std::size_t packetFooterBytes = 0;
    std::size_t samplesPerPacket = 256;
    std::size_t alignmentBytes = 0;
};

struct ThroughputTarget
{
    std::uint64_t targetBytesPerSecond = 90'000'000;
    std::chrono::milliseconds batchPeriod{10};
    PayloadAccountingMode mode = PayloadAccountingMode::RawBcoBytes;
    SimulatedBcoPacketModel packetModel;
};

struct SimulatorBcoLoadConfig
{
    SimulatorLoadProfile profile = SimulatorLoadProfile::UiDemo;

    std::size_t samplesPerSecond = 1'280;
    std::chrono::milliseconds batchPeriod{100};
    std::size_t samplesPerBatchMultiplier = 1;
    std::optional<ThroughputTarget> throughputTarget;

    bool deterministic = true;
    bool burstModeEnabled = false;
    double burstMultiplier = 1.0;
    std::chrono::milliseconds burstDuration{0};
    std::chrono::milliseconds calmDuration{0};

    int minVisibleAmplitude = 0;
    std::vector<SimulatorPulseBandConfig> pulseBandConfigs;
};

struct HardwareProfile
{
    DataSourceMode dataSourceMode = DataSourceMode::Simulator;

    BcoStreamConfig bcoStreamConfig;
    SimulatorBcoLoadConfig simulatorLoadConfig;
};

} // namespace siriusscope::hardware
