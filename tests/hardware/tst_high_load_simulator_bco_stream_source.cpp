#include "hardware/data_source_factory.h"
#include "hardware/interfaces/antenna_azimuth_provider.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/simulator/simulated_bco_payload_accounting.h"

#include <algorithm>
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

class FakeAntennaAzimuthProvider final : public hardware::IAntennaAzimuthProvider
{
public:
    explicit FakeAntennaAzimuthProvider(double azimuthDeg)
        : m_azimuthDeg(azimuthDeg)
    {
    }

    double currentAzimuthDeg() const override { return m_azimuthDeg; }

private:
    double m_azimuthDeg = 0.0;
};

class CapturingDiagnosticsSink final : public infrastructure::IDiagnosticsSink
{
public:
    void publish(const infrastructure::DiagnosticEvent& event) override
    {
        events.push_back(event);
    }

    std::vector<infrastructure::DiagnosticEvent> events;
};

bool hasWarningContaining(const CapturingDiagnosticsSink& diagnostics,
                          const std::string& text)
{
    return std::any_of(diagnostics.events.begin(),
                       diagnostics.events.end(),
                       [&text](const auto& event) {
                           return event.severity == infrastructure::DiagnosticSeverity::Warning
                               && event.message.find(text) != std::string::npos;
                       });
}

core::BandConfig makeBandConfig(bool enabled = true)
{
    const auto created = core::BandConfig::create(0,
                                                  3'000'000'000LL,
                                                  500'000'000LL,
                                                  enabled);
    return *created.value();
}

core::BandConfig makeBandConfig(int bandIndex,
                                std::int64_t centerFrequencyHz,
                                bool enabled = true)
{
    const auto created = core::BandConfig::create(bandIndex,
                                                  centerFrequencyHz,
                                                  500'000'000LL,
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

hardware::SimulatorBcoLoadConfig makeTargetRawLoadConfig()
{
    hardware::SimulatorBcoLoadConfig loadConfig;
    loadConfig.profile = hardware::SimulatorLoadProfile::TargetRawThroughput90MBps;
    loadConfig.minVisibleAmplitude = 1;
    return loadConfig;
}

hardware::SimulatorBcoLoadConfig makeBaselineRawLoadConfig()
{
    hardware::SimulatorBcoLoadConfig loadConfig;
    loadConfig.profile = hardware::SimulatorLoadProfile::BaselineRawThroughput60MBps;
    loadConfig.minVisibleAmplitude = 1;
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

int peakForBeam(const hardware::BcoSampleBlock& block, int beamIndex)
{
    int peak = 0;
    for (const auto& sample : block.samples) {
        if (sample.beamIndex == beamIndex) {
            peak = std::max(peak, sample.amplitude);
        }
    }
    return peak;
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
    test.require(firstBlock->samples.size() <= 10,
                 "continuous generation respects requested emitted sample budget");
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
                     "generated sampleIndex is monotonic nondecreasing");
        previousSampleIndex = sample.sampleIndex;
        hasPreviousSample = true;

        test.require(sample.bandIndex == band.bandIndex,
                     "generated sample uses configured bandIndex");
        test.require(sample.beamIndex == 0 || sample.beamIndex == 1,
                     "generated sample beamIndex is 0 or 1");
        test.require(sample.amplitude >= core::DomainConstraints::minAmplitude
                         && sample.amplitude <= core::DomainConstraints::maxAmplitude,
                     "generated sample amplitude follows domain range");
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

void testAntennaAzimuthControlsBeamBalance(TestRunner& test)
{
    const auto collectForAzimuth = [](double azimuthDeg) {
        FakeAntennaAzimuthProvider antenna(azimuthDeg);
        hardware::HighLoadSimulatorBcoStreamSource source(makeShortLoadConfig(),
                                                          nullptr,
                                                          &antenna);
        const auto configured = source.configure(makeValidConfig());
        std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
        const bool arrived = configured.success && collectBlocks(source, 1, blocks);
        if (!arrived || blocks.empty()) {
            return hardware::IBcoStreamSource::SampleBlockPtr{};
        }
        return blocks.front();
    };

    const auto beam0Dominant = collectForAzimuth(75.0);
    const auto beam1Dominant = collectForAzimuth(15.0);
    const auto balanced = collectForAzimuth(45.0);

    test.require(beam0Dominant != nullptr, "antenna-aware source emits beam0-dominant block");
    test.require(beam1Dominant != nullptr, "antenna-aware source emits beam1-dominant block");
    test.require(balanced != nullptr, "antenna-aware source emits balanced block");
    if (!beam0Dominant || !beam1Dominant || !balanced) {
        return;
    }

    test.require(beam0Dominant->stats.antennaAzimuthDeg
                     && *beam0Dominant->stats.antennaAzimuthDeg == 75.0,
                 "block stats carry antenna azimuth metadata");
    test.require(peakForBeam(*beam0Dominant, 0) > peakForBeam(*beam0Dominant, 1),
                 "antenna 75 deg makes default source stronger in beam 0");
    test.require(peakForBeam(*beam1Dominant, 1) > peakForBeam(*beam1Dominant, 0),
                 "antenna 15 deg makes default source stronger in beam 1");
    test.require(std::abs(peakForBeam(*balanced, 0) - peakForBeam(*balanced, 1)) <= 1,
                 "antenna 45 deg balances beam 0 and beam 1 peaks");
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
        makeBandConfig(1, 5'795'000'000LL),
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

void testPulseConfigsCanBeUpdated(TestRunner& test)
{
    hardware::HighLoadSimulatorBcoStreamSource source(makeShortLoadConfig());
    std::vector<hardware::SimulatorPulseBandConfig> configs{
        hardware::SimulatorPulseBandConfig{0, true, 50.0, 10.0},
    };

    source.setPulseBandConfigs(configs);
    const auto stored = source.pulseBandConfigs();

    test.require(stored.size() == 1, "source stores updated pulse config count");
    if (!stored.empty()) {
        test.require(stored.front().bandIndex == 0,
                     "source stores updated pulse config band index");
        test.require(stored.front().pulsePeriodUs == 50.0,
                     "source stores updated pulse period");
        test.require(stored.front().pulseWidthUs == 10.0,
                     "source stores updated pulse width");
    }
}

void testRuntimePulseConfigUpdateAppliesToNextBlocks(TestRunner& test)
{
    auto loadConfig = makePulseLoadConfig();
    loadConfig.pulseBandConfigs.clear();
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);

    auto config = makeValidConfig();
    config.timeBase.samplePeriodNs = 1000;
    const auto configureResult = source.configure(config);

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;

    const auto startResult = source.start([&](auto block) {
        {
            std::lock_guard lock(mutex);
            blocks.push_back(std::move(block));
            if (blocks.size() == 1) {
                source.setPulseBandConfigs({
                    hardware::SimulatorPulseBandConfig{0, false, 10.0, 3.0},
                });
            }
        }
        condition.notify_one();
    });

    std::unique_lock lock(mutex);
    const bool arrived = condition.wait_for(lock, std::chrono::milliseconds{500}, [&] {
        return blocks.size() >= 2;
    });
    lock.unlock();

    source.stop();

    test.require(configureResult.success,
                 "runtime pulse update source accepts valid config");
    test.require(startResult.success,
                 "runtime pulse update source starts");
    test.require(arrived,
                 "runtime pulse update source emits blocks before and after update");
    test.require(blocks.size() >= 2 && blocks[0] && blocks[1],
                 "runtime pulse update source has two non-null blocks");
    if (blocks.size() < 2 || !blocks[0] || !blocks[1]) {
        return;
    }

    test.require(!blocks[0]->samples.empty(),
                 "source emits continuous samples before pulse config update");
    test.require(blocks[1]->samples.empty(),
                 "source applies disabled pulse config to following block");
}

void testTargetRawFastPathAssignsMonotonicSampleIndexes(TestRunner& test)
{
    auto config = makeValidConfig();
    config.timeBase.firstSampleIndex = 1000;

    hardware::HighLoadSimulatorBcoStreamSource source(makeTargetRawLoadConfig());
    const auto configureResult = source.configure(config);

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = configureResult.success
        && collectBlocks(source, 1, blocks, std::chrono::milliseconds{1000});

    test.require(configureResult.success, "target raw source accepts stream config");
    test.require(arrived, "target raw source emits a fast-path block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "target raw block is available");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    const auto& block = *blocks.front();
    const hardware::ThroughputTarget target;

    test.require(block.samples.size() == hardware::samplesPerBatchForTarget(target),
                 "target raw block uses packet-aligned sample count");
    test.require(block.samples.size() > 1000,
                 "target raw block has enough samples to audit indexes");
    test.require(block.stats.sampleCount == block.samples.size(),
                 "target raw stats sample count matches samples");
    test.require(block.stats.firstSampleIndex == 1000,
                 "target raw stats start at timebase first sample index");
    test.require(block.stats.lastSampleIndex
                     == block.stats.firstSampleIndex + block.samples.size() - 1,
                 "target raw stats cover generated sample range");
    test.require(block.stats.packetCount == hardware::packetsPerBatchForTarget(target),
                 "target raw stats packet count uses throughput packet model");

    bool contiguousSampleIndexes = true;
    for (std::size_t i = 0; i < block.samples.size(); ++i) {
        if (block.samples[i].sampleIndex
            != block.stats.firstSampleIndex + static_cast<std::uint64_t>(i)) {
            contiguousSampleIndexes = false;
            break;
        }
    }
    test.require(contiguousSampleIndexes,
                 "target raw fast path sampleIndex is contiguous");
}

void testTargetRawDefaultBatchMultiplierPreservesBatchSize(TestRunner& test)
{
    hardware::HighLoadSimulatorBcoStreamSource source(makeTargetRawLoadConfig());
    const auto configureResult = source.configure(makeValidConfig());

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = configureResult.success
        && collectBlocks(source, 1, blocks, std::chrono::milliseconds{1000});

    const hardware::ThroughputTarget target;
    test.require(configureResult.success,
                 "default target raw multiplier source accepts stream config");
    test.require(arrived,
                 "default target raw multiplier source emits block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "default target raw multiplier block is available");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    test.require(blocks.front()->samples.size() == hardware::samplesPerBatchForTarget(target),
                 "default target raw multiplier preserves base batch size");
    test.require(blocks.front()->stats.packetCount
                     == hardware::packetsPerBatchForTarget(target),
                 "default target raw multiplier preserves base packet count");
}

void testTargetRawBatchMultiplierChangesBatchSize(TestRunner& test)
{
    auto loadConfig = makeTargetRawLoadConfig();
    loadConfig.samplesPerBatchMultiplier = 4;
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);
    const auto configureResult = source.configure(makeValidConfig());

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = configureResult.success
        && collectBlocks(source, 1, blocks, std::chrono::milliseconds{1000});

    const hardware::ThroughputTarget target;
    const auto baseSamplesPerBatch = hardware::samplesPerBatchForTarget(target);
    const auto basePacketsPerBatch = hardware::packetsPerBatchForTarget(target);
    test.require(configureResult.success,
                 "target raw batch multiplier source accepts stream config");
    test.require(arrived,
                 "target raw batch multiplier source emits block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "target raw batch multiplier block is available");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    test.require(blocks.front()->samples.size() == baseSamplesPerBatch * 4,
                 "target raw multiplier scales samples per batch");
    test.require(blocks.front()->stats.packetCount == basePacketsPerBatch * 4,
                 "target raw multiplier scales packet count per batch");
}

void testTargetRawBatchMultiplierKeepsSampleIndexesContiguousAcrossBlocks(
    TestRunner& test)
{
    auto streamConfig = makeValidConfig();
    streamConfig.timeBase.firstSampleIndex = 5000;

    auto loadConfig = makeTargetRawLoadConfig();
    loadConfig.samplesPerBatchMultiplier = 4;
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);
    const auto configureResult = source.configure(streamConfig);

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = configureResult.success
        && collectBlocks(source, 3, blocks, std::chrono::milliseconds{1500});

    test.require(configureResult.success,
                 "target raw contiguous multiplier source accepts stream config");
    test.require(arrived,
                 "target raw contiguous multiplier source emits several blocks");
    test.require(blocks.size() >= 3,
                 "target raw contiguous multiplier captured three blocks");
    if (blocks.size() < 3 || !blocks[0] || !blocks[1] || !blocks[2]) {
        return;
    }

    test.require(blocks[0]->stats.firstSampleIndex == 5000,
                 "target raw multiplier first block starts at configured sample index");
    for (std::size_t index = 1; index < 3; ++index) {
        test.require(blocks[index]->stats.firstSampleIndex
                         == blocks[index - 1]->stats.lastSampleIndex + 1,
                     "target raw multiplier keeps sampleIndex contiguous between blocks");
    }
}

void testTargetRawInvalidBatchMultiplierFallsBackToDefault(TestRunner& test)
{
    const hardware::ThroughputTarget target;
    for (const auto invalidMultiplier : {std::size_t{0}, std::size_t{3}}) {
        auto loadConfig = makeTargetRawLoadConfig();
        loadConfig.samplesPerBatchMultiplier = invalidMultiplier;
        hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);
        const auto configureResult = source.configure(makeValidConfig());

        std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
        const bool arrived = configureResult.success
            && collectBlocks(source, 1, blocks, std::chrono::milliseconds{1000});

        test.require(configureResult.success,
                     "invalid target raw multiplier source accepts stream config");
        test.require(arrived,
                     "invalid target raw multiplier source emits block");
        test.require(!blocks.empty() && blocks.front() != nullptr,
                     "invalid target raw multiplier block is available");
        if (blocks.empty() || !blocks.front()) {
            continue;
        }

        test.require(blocks.front()->samples.size()
                         == hardware::samplesPerBatchForTarget(target),
                     "invalid target raw multiplier falls back to base batch size");
    }
}

void testBaselineRaw60MbpsProfileEmitsPacketAlignedBatch(TestRunner& test)
{
    hardware::HighLoadSimulatorBcoStreamSource source(makeBaselineRawLoadConfig());
    const auto configureResult = source.configure(makeValidConfig());

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = configureResult.success
        && collectBlocks(source, 1, blocks, std::chrono::milliseconds{1000});

    const auto target = hardware::baselineRawThroughput60MbpsTarget();
    test.require(configureResult.success,
                 "baseline raw source accepts stream config");
    test.require(arrived, "baseline raw source emits block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "baseline raw block is available");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    test.require(blocks.front()->samples.size() == hardware::samplesPerBatchForTarget(target),
                 "baseline raw source emits packet-aligned sample count");
    test.require(blocks.front()->stats.packetCount
                     == hardware::packetsPerBatchForTarget(target),
                 "baseline raw source reports packet-aligned packet count");
    test.require(hardware::rawBytesForSamples(blocks.front()->samples.size(),
                                             target.packetModel)
                     == 598'560,
                 "baseline raw source accounts expected raw bytes per batch");
}

void testBaselineRaw60MbpsPulseConfigCannotExceedBatchBudget(TestRunner& test)
{
    auto loadConfig = makeBaselineRawLoadConfig();
    loadConfig.pulseBandConfigs = {hardware::SimulatorPulseBandConfig{
        0,
        true,
        1.001,
        1.0,
    }};
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig);
    const auto configureResult = source.configure(makeValidConfig());

    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> blocks;
    const bool arrived = configureResult.success
        && collectBlocks(source, 1, blocks, std::chrono::milliseconds{1000});

    const auto target = hardware::baselineRawThroughput60MbpsTarget();
    const auto baselineSamplesPerBatch = hardware::samplesPerBatchForTarget(target);
    test.require(configureResult.success,
                 "baseline pulse source accepts stream config");
    test.require(arrived, "baseline pulse source emits block");
    test.require(!blocks.empty() && blocks.front() != nullptr,
                 "baseline pulse block is available");
    if (blocks.empty() || !blocks.front()) {
        return;
    }

    test.require(blocks.front()->samples.size() <= baselineSamplesPerBatch,
                 "extreme pulse settings cannot exceed baseline samples per batch");
    test.require(blocks.front()->stats.packetCount
                     <= hardware::packetsPerBatchForTarget(target),
                 "extreme pulse settings cannot exceed baseline packet budget");
}

void testConfigurePublishesTimebaseMismatchWarning(TestRunner& test)
{
    auto loadConfig = makePulseLoadConfig();
    loadConfig.samplesPerSecond = 1'000'000;
    CapturingDiagnosticsSink diagnostics;
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig, &diagnostics);

    auto config = makeValidConfig();
    config.timeBase.samplePeriodNs = 320;
    const auto configureResult = source.configure(config);

    test.require(configureResult.success,
                 "source accepts config with diagnostic timebase mismatch");
    test.require(hasWarningContaining(diagnostics, "timebase mismatch"),
                 "source publishes warning for simulator timebase mismatch");
    test.require(hasWarningContaining(diagnostics, "samplesPerSecond=1000000"),
                 "timebase mismatch warning contains configured sample rate");
    test.require(hasWarningContaining(diagnostics, "samplePeriodNs=320"),
                 "timebase mismatch warning contains sample period");
}

void testConfigureDoesNotWarnWhenTimebaseMatchesThroughput(TestRunner& test)
{
    auto loadConfig = makePulseLoadConfig();
    loadConfig.samplesPerSecond = 1'000'000;
    CapturingDiagnosticsSink diagnostics;
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig, &diagnostics);

    auto config = makeValidConfig();
    config.timeBase.samplePeriodNs = 1000;
    const auto configureResult = source.configure(config);

    test.require(configureResult.success,
                 "source accepts config with matching simulator timebase");
    test.require(!hasWarningContaining(diagnostics, "timebase mismatch"),
                 "source does not publish mismatch warning when rates match");
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
    testAntennaAzimuthControlsBeamBalance(test);
    testPulseConfigGatesSamples(test);
    testDisabledPulseConfigSuppressesBand(test);
    testDifferentBandsUseIndependentPulseConfigs(test);
    testSampleIndexAdvancesThroughPulsePauses(test);
    testStopIsIdempotent(test);
    testPulseConfigsCanBeUpdated(test);
    testRuntimePulseConfigUpdateAppliesToNextBlocks(test);
    testTargetRawFastPathAssignsMonotonicSampleIndexes(test);
    testTargetRawDefaultBatchMultiplierPreservesBatchSize(test);
    testTargetRawBatchMultiplierChangesBatchSize(test);
    testTargetRawBatchMultiplierKeepsSampleIndexesContiguousAcrossBlocks(test);
    testTargetRawInvalidBatchMultiplierFallsBackToDefault(test);
    testBaselineRaw60MbpsProfileEmitsPacketAlignedBatch(test);
    testBaselineRaw60MbpsPulseConfigCannotExceedBatchBudget(test);
    testConfigurePublishesTimebaseMismatchWarning(test);
    testConfigureDoesNotWarnWhenTimebaseMatchesThroughput(test);
    testFactoryCreatesHighLoadSimulator(test);
    testFactoryRejectsWrongMode(test);

    return test.result();
}
