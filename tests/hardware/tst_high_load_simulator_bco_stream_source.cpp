#include "hardware/data_source_factory.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"

#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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

core::BandConfig makeBandConfig(int bandIndex,
                                std::int64_t centerFrequencyHz,
                                bool enabled = true)
{
    const auto created = core::BandConfig::create(bandIndex,
                                                  centerFrequencyHz,
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

hardware::SimulatorBcoLoadConfig makePulseLoadConfig()
{
    hardware::SimulatorBcoLoadConfig loadConfig;
    loadConfig.profile = hardware::SimulatorLoadProfile::UiDemo;
    loadConfig.samplesPerSecond = 1'000'000;
    loadConfig.batchPeriod = std::chrono::milliseconds{1};
    return loadConfig;
}

bool collectBlocks(hardware::HighLoadSimulatorBcoStreamSource& source,
                   std::size_t targetBlockCount,
                   std::vector<hardware::IBcoStreamSource::SampleBlockPtr>& blocks,
                   std::chrono::milliseconds timeout = std::chrono::milliseconds{500})
{
    std::mutex mutex;
    std::condition_variable condition;

    const auto startResult = source.start([&](auto block) {
        {
            std::lock_guard lock(mutex);
            blocks.push_back(std::move(block));
        }
        condition.notify_one();
    });
    if (!startResult) {
        return false;
    }

    std::unique_lock lock(mutex);
    const bool arrived = condition.wait_for(lock, timeout, [&] {
        return blocks.size() >= targetBlockCount;
    });
    lock.unlock();

    source.stop();
    return arrived;
}

bool isInsidePulseWindow(const core::SignalSample& sample,
                         std::uint64_t firstSampleIndex,
                         std::uint64_t samplePeriodNs,
                         double pulsePeriodUs,
                         double pulseWidthUs)
{
    const auto relativeSampleIndex = sample.sampleIndex - firstSampleIndex;
    const long double relativeNs =
        static_cast<long double>(relativeSampleIndex)
        * static_cast<long double>(samplePeriodNs);
    const long double periodNs = static_cast<long double>(pulsePeriodUs) * 1000.0L;
    const long double widthNs = static_cast<long double>(pulseWidthUs) * 1000.0L;
    const auto phaseNs = std::fmod(relativeNs, periodNs);
    return phaseNs >= 0.0L && phaseNs < widthNs;
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
    const auto loadConfig = makeShortLoadConfig();
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);
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

    test.require(loadConfig.pulseBandConfigs.empty(),
                 "continuous high-load test uses empty pulse configs");
    test.require(!firstBlock->samples.empty(), "generated block has samples");
    test.require(firstBlock->samples.size() == 10,
                 "continuous generation keeps requested samples per batch");
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
    std::uint64_t expectedSampleIndex = config.timeBase.firstSampleIndex;
    for (const auto& sample : firstBlock->samples) {
        test.require(!hasPreviousSample || sample.sampleIndex >= previousSampleIndex,
                     "generated sampleIndex is monotonic");
        test.require(sample.sampleIndex == expectedSampleIndex,
                     "continuous generation keeps contiguous sampleIndex values");
        previousSampleIndex = sample.sampleIndex;
        hasPreviousSample = true;
        ++expectedSampleIndex;

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

void testPulseConfigGatesSamples(TestRunner& test)
{
    auto loadConfig = makePulseLoadConfig();
    loadConfig.pulseBandConfigs = {hardware::SimulatorPulseBandConfig{0, true, 10.0, 3.0}};
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);

    auto config = makeValidConfig();
    config.timeBase.samplePeriodNs = 1000;
    const auto configureResult = source.configure(config);

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = collectBlocks(source, 1, blocks);

    test.require(configureResult.success, "pulse-gated source accepts valid config");
    test.require(arrived, "pulse-gated source emits a block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "pulse-gated source block is not null");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    const auto& block = *blocks.front();
    test.require(!block.samples.empty(), "pulse-gated source creates samples");
    for (const auto& sample : block.samples) {
        test.require(isInsidePulseWindow(sample,
                                         config.timeBase.firstSampleIndex,
                                         config.timeBase.samplePeriodNs,
                                         10.0,
                                         3.0),
                     "pulse-gated sample is inside configured pulse window");
    }
}

void testDisabledPulseConfigSuppressesBand(TestRunner& test)
{
    auto loadConfig = makePulseLoadConfig();
    loadConfig.pulseBandConfigs = {hardware::SimulatorPulseBandConfig{0, false, 10.0, 3.0}};
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);

    auto config = makeValidConfig();
    config.timeBase.samplePeriodNs = 1000;
    const auto configureResult = source.configure(config);

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = collectBlocks(source, 1, blocks);

    test.require(configureResult.success, "disabled pulse config source accepts valid config");
    test.require(arrived, "disabled pulse config source emits an empty block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "disabled pulse config block is not null");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    const auto& block = *blocks.front();
    test.require(block.samples.empty(), "disabled pulse config suppresses the only band");
    test.require(block.stats.sampleCount == 0,
                 "disabled pulse config reports zero generated samples");
    test.require(block.stats.packetCount == 0,
                 "disabled pulse config reports zero packets");
    test.require(block.stats.firstSampleIndex == config.timeBase.firstSampleIndex,
                 "empty block reports batch start as first sample index");
    test.require(block.stats.lastSampleIndex == config.timeBase.firstSampleIndex,
                 "empty block reports batch start as last sample index");
}

void testDifferentBandsUseIndependentPulseConfigs(TestRunner& test)
{
    auto loadConfig = makePulseLoadConfig();
    loadConfig.pulseBandConfigs = {
        hardware::SimulatorPulseBandConfig{0, true, 10.0, 3.0},
        hardware::SimulatorPulseBandConfig{1, true, 20.0, 5.0},
    };
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);

    hardware::BcoStreamConfig config;
    config.bandConfigs = {
        makeBandConfig(0, 3'000'000'000LL),
        makeBandConfig(1, 3'200'000'000LL),
    };
    config.timeBase.firstSampleIndex = 42;
    config.timeBase.samplePeriodNs = 1000;
    config.sessionId = 77;
    const auto configureResult = source.configure(config);

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = collectBlocks(source, 1, blocks);

    test.require(configureResult.success, "independent pulse source accepts valid config");
    test.require(arrived, "independent pulse source emits a block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "independent pulse source block is not null");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    bool sawBand0 = false;
    bool sawBand1 = false;
    for (const auto& sample : blocks.front()->samples) {
        if (sample.bandIndex == 0) {
            sawBand0 = true;
            test.require(isInsidePulseWindow(sample,
                                             config.timeBase.firstSampleIndex,
                                             config.timeBase.samplePeriodNs,
                                             10.0,
                                             3.0),
                         "band 0 sample is inside band 0 pulse window");
        } else if (sample.bandIndex == 1) {
            sawBand1 = true;
            test.require(isInsidePulseWindow(sample,
                                             config.timeBase.firstSampleIndex,
                                             config.timeBase.samplePeriodNs,
                                             20.0,
                                             5.0),
                         "band 1 sample is inside band 1 pulse window");
        } else {
            test.require(false, "independent pulse source emits only configured bands");
        }
    }

    test.require(sawBand0, "independent pulse source emits band 0 samples");
    test.require(sawBand1, "independent pulse source emits band 1 samples");
}

void testSampleIndexAdvancesThroughPulsePauses(TestRunner& test)
{
    auto loadConfig = makePulseLoadConfig();
    loadConfig.pulseBandConfigs = {hardware::SimulatorPulseBandConfig{0, true, 100.0, 1.0}};
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);

    auto config = makeValidConfig();
    config.timeBase.samplePeriodNs = 1000;
    const auto configureResult = source.configure(config);

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = collectBlocks(source, 2, blocks);

    test.require(configureResult.success, "pause-advance source accepts valid config");
    test.require(arrived, "pause-advance source emits two blocks");
    test.require(blocks.size() >= 2 && blocks[0] && blocks[1],
                 "pause-advance source has two non-null blocks");
    if (blocks.size() < 2 || !blocks[0] || !blocks[1]) {
        return;
    }

    test.require(!blocks[0]->samples.empty() && !blocks[1]->samples.empty(),
                 "pause-advance blocks contain visible pulse samples");
    if (blocks[0]->samples.empty() || blocks[1]->samples.empty()) {
        return;
    }

    const auto firstBlockLastSampleIndex = blocks[0]->samples.back().sampleIndex;
    const auto secondBlockFirstSampleIndex = blocks[1]->samples.front().sampleIndex;
    test.require(secondBlockFirstSampleIndex > firstBlockLastSampleIndex + 1,
                 "sampleIndex advances through pulse pauses between blocks");
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
    testPulseConfigGatesSamples(test);
    testDisabledPulseConfigSuppressesBand(test);
    testDifferentBandsUseIndependentPulseConfigs(test);
    testSampleIndexAdvancesThroughPulsePauses(test);
    testStopIsIdempotent(test);
    testFactoryCreatesHighLoadSimulator(test);
    testFactoryRejectsWrongMode(test);

    return test.result();
}
