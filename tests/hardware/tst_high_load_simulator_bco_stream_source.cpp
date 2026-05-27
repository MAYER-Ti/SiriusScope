#include "hardware/data_source_factory.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace {

using namespace siriusscope;

class TestRunner
{
public:
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            ++m_failed;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

core::BandConfig makeBandConfig(bool enabled = true)
{
    const auto created = core::BandConfig::create(0,
                                                  3'000'000'000LL,
                                                  100'000'000LL,
                                                  enabled);
    return *created.value();
}

hardware::BcoStreamConfig makeValidConfig()
{
    hardware::BcoStreamConfig config;
    config.bandConfigs = {makeBandConfig()};
    config.timeBase.firstSampleIndex = 42;
    config.sessionId = 77;
    return config;
}

hardware::SimulatorBcoLoadConfig makeShortLoadConfig()
{
    hardware::SimulatorBcoLoadConfig loadConfig;
    loadConfig.profile = hardware::SimulatorLoadProfile::UiDemo;
    loadConfig.samplesPerSecond = 1'000;
    loadConfig.batchPeriod = std::chrono::milliseconds{10};
    return loadConfig;
}

void testConfigureRejectsEmptyBands(TestRunner& test)
{
    hardware::HighLoadSimulatorBcoStreamSource source;
    const hardware::BcoStreamConfig config;

    const auto result = source.configure(config);

    test.require(!result, "configure rejects empty band configs");
    test.require(!result.message.empty(), "empty band rejection has a message");
}

void testConfigureRejectsAllDisabledBands(TestRunner& test)
{
    hardware::HighLoadSimulatorBcoStreamSource source;
    hardware::BcoStreamConfig config;
    config.bandConfigs = {makeBandConfig(false)};

    const auto result = source.configure(config);

    test.require(!result, "configure rejects all disabled bands");
    test.require(!result.message.empty(), "all disabled rejection has a message");
}

void testSourceGeneratesBlocks(TestRunner& test)
{
    hardware::HighLoadSimulatorBcoStreamSource source(makeShortLoadConfig());
    const auto config = makeValidConfig();
    const auto configureResult = source.configure(config);

    std::mutex mutex;
    std::condition_variable condition;
    hardware::IBcoStreamSource::SampleBlockPtr firstBlock;
    bool received = false;

    const auto startResult = source.start([&](auto block) {
        {
            std::lock_guard lock(mutex);
            if (!received) {
                firstBlock = std::move(block);
                received = true;
            }
        }
        condition.notify_one();
    });

    std::unique_lock lock(mutex);
    const bool arrived = condition.wait_for(lock, std::chrono::milliseconds{500}, [&] {
        return received;
    });
    lock.unlock();

    const auto stopResult = source.stop();
    const auto metrics = source.metrics();

    test.require(configureResult.success, "source accepts valid config");
    test.require(startResult.success, "source starts after configure");
    test.require(arrived, "source emits a block");
    test.require(stopResult.success, "source stops after generation");
    test.require(firstBlock != nullptr, "generated block is not null");
    if (!firstBlock) {
        return;
    }

    test.require(!firstBlock->samples.empty(), "generated block has samples");
    test.require(firstBlock->stats.sampleCount == firstBlock->samples.size(),
                 "block stats sample count matches samples");
    test.require(firstBlock->stats.firstSampleIndex <= firstBlock->stats.lastSampleIndex,
                 "block stats sample index range is ordered");
    test.require(firstBlock->stats.packetCount > 0, "block stats count packets");
    test.require(firstBlock->stats.lostPacketCount == 0, "block stats have no lost packets");
    test.require(firstBlock->stats.malformedPacketCount == 0,
                 "block stats have no malformed packets");

    const auto& band = config.bandConfigs.front();
    std::uint64_t previousSampleIndex = 0;
    bool hasPreviousSample = false;
    for (const auto& sample : firstBlock->samples) {
        test.require(!hasPreviousSample || sample.sampleIndex >= previousSampleIndex,
                     "generated sampleIndex is monotonic");
        previousSampleIndex = sample.sampleIndex;
        hasPreviousSample = true;

        test.require(sample.bandIndex == band.bandIndex,
                     "generated sample uses configured bandIndex");
        test.require(sample.beamIndex == 0 || sample.beamIndex == 1,
                     "generated sample beamIndex is 0 or 1");
        test.require(sample.amplitude >= 20 && sample.amplitude <= 119,
                     "generated sample amplitude follows simulator range");
        test.require(band.containsFrequency(sample.absoluteFrequencyHz),
                     "generated sample frequency is inside band");
        test.require(sample.absoluteFrequencyHz
                         == band.centerFrequencyHz + sample.frequencyOffsetHz,
                     "generated sample absolute frequency matches offset");
        test.require(sample.validate(band).isValid(),
                     "generated sample passes domain validation");
    }

    test.require(metrics.producedSamples > 0, "metrics count produced samples");
    test.require(metrics.producedBatches > 0, "metrics count produced batches");
    test.require(metrics.producedSamplesPerSecond >= 0.0,
                 "metrics expose sample rate");
    test.require(metrics.equivalentMegabytesPerSecond >= 0.0,
                 "metrics expose equivalent throughput");
}

void testStopIsIdempotent(TestRunner& test)
{
    hardware::HighLoadSimulatorBcoStreamSource source(makeShortLoadConfig());
    const auto configureResult = source.configure(makeValidConfig());
    const auto stopBeforeStart = source.stop();
    const auto startResult = source.start([](auto) {});
    const auto firstStop = source.stop();
    const auto secondStop = source.stop();

    test.require(configureResult.success, "source configures before idempotent stop test");
    test.require(stopBeforeStart.success, "stop before start succeeds");
    test.require(startResult.success, "source starts in idempotent stop test");
    test.require(firstStop.success, "first stop succeeds");
    test.require(secondStop.success, "second stop succeeds");
}

void testFactoryCreatesHighLoadSimulator(TestRunner& test)
{
    hardware::HardwareProfile profile;
    profile.dataSourceMode = hardware::DataSourceMode::Simulator;
    profile.bcoStreamConfig = makeValidConfig();
    profile.simulatorLoadConfig.profile = hardware::SimulatorLoadProfile::UiDemo;

    auto source = hardware::DataSourceFactory::createHighLoadSimulatorBcoStreamSource(profile);

    test.require(source != nullptr, "factory creates high-load simulator source");
}

void testFactoryRejectsWrongMode(TestRunner& test)
{
    hardware::HardwareProfile profile;
    profile.dataSourceMode = hardware::DataSourceMode::RealHardware;
    profile.bcoStreamConfig = makeValidConfig();

    auto source = hardware::DataSourceFactory::createHighLoadSimulatorBcoStreamSource(profile);

    test.require(source == nullptr, "factory rejects high-load simulator for real mode");
}

} // namespace

int main()
{
    TestRunner test;

    testConfigureRejectsEmptyBands(test);
    testConfigureRejectsAllDisabledBands(test);
    testSourceGeneratesBlocks(test);
    testStopIsIdempotent(test);
    testFactoryCreatesHighLoadSimulator(test);
    testFactoryRejectsWrongMode(test);

    return test.result();
}
