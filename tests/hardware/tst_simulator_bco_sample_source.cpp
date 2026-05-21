#include "hardware/simulator/simulator_bco_sample_source.h"
#include "hardware/simulator/simulator_antenna_state.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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

bool waitForBatch(hardware::SimulatorBcoSampleSource& source,
                  hardware::BcoSampleBatch& firstBatch,
                  std::atomic<int>& callbackCount)
{
    std::mutex mutex;
    std::condition_variable condition;
    bool received = false;

    const auto startResult = source.start([&](const hardware::BcoSampleBatch& batch) {
        ++callbackCount;
        {
            std::lock_guard lock(mutex);
            if (!received) {
                firstBatch = batch;
                received = true;
            }
        }
        condition.notify_one();
    });

    if (!startResult) {
        return false;
    }

    std::unique_lock lock(mutex);
    return condition.wait_for(lock, std::chrono::milliseconds(500), [&] { return received; });
}

core::BandConfig makeBandConfig(int bandIndex,
                                std::int64_t centerHz,
                                std::int64_t widthHz = 500'000'000LL)
{
    const auto created = core::BandConfig::create(bandIndex, centerHz, widthHz);
    return *created.value();
}

void testDefaultPulseConfigsExist(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source;
    const auto configs = source.pulseBandConfigs();

    test.require(static_cast<int>(configs.size()) == core::DomainConstraints::currentBandCount,
                 "default pulse configs exist for current bands");

    const auto band0 = std::find_if(configs.begin(), configs.end(), [](const auto& config) {
        return config.bandIndex == 0;
    });
    test.require(band0 != configs.end(), "default pulse config contains band 0");
    if (band0 != configs.end()) {
        test.require(band0->enabled, "default pulse config for band 0 is enabled");
        test.require(band0->pulsePeriodUs == 100000.0,
                     "default pulse period for band 0 is 100000 us");
        test.require(band0->pulseWidthUs == 10000.0,
                     "default pulse width for band 0 is 10000 us");
    }
}

void testPulseMaskRestrictsSamplesToActiveWindow(TestRunner& test)
{
    hardware::SimulatorAntennaState state(45.0);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 32, 0, 1},
        &state);

    auto pulseConfigs = source.pulseBandConfigs();
    for (auto& config : pulseConfigs) {
        config.pulsePeriodUs = 1000.0;
        config.pulseWidthUs = 100.0;
    }
    source.setPulseBandConfigs(pulseConfigs);

    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;
    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(received, "pulse mask source produces a batch in the active window");
    test.require(!firstBatch.samples.empty(), "pulse mask batch is not empty");
    for (const auto& sample : firstBatch.samples) {
        const double phaseNs = std::fmod(
            static_cast<double>(sample.sampleIndex)
                * static_cast<double>(core::DomainConstraints::defaultSamplePeriodNs),
            1000.0 * 1000.0);
        test.require(phaseNs >= 0.0 && phaseNs < 100.0 * 1000.0,
                     "pulse mask emits samples only inside active pulse width");
    }
}

void testDisabledPulseBandProducesNoSamplesForBand(TestRunner& test)
{
    hardware::SimulatorAntennaState state(45.0);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 32, 0, 1},
        &state);

    auto pulseConfigs = source.pulseBandConfigs();
    for (auto& config : pulseConfigs) {
        if (config.bandIndex == 0) {
            config.enabled = false;
        }
    }
    source.setPulseBandConfigs(pulseConfigs);

    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;
    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(received, "disabled pulse band still allows other bands to produce a batch");
    for (const auto& sample : firstBatch.samples) {
        test.require(sample.bandIndex != 0, "disabled pulse band produces no samples");
    }
}

void testInvalidPulseWidthProducesNoSamplesForBand(TestRunner& test)
{
    hardware::SimulatorAntennaState state(45.0);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 32, 0, 1},
        &state);

    auto pulseConfigs = source.pulseBandConfigs();
    for (auto& config : pulseConfigs) {
        if (config.bandIndex == 0) {
            config.pulsePeriodUs = 1000.0;
            config.pulseWidthUs = 1000.0;
        }
    }
    source.setPulseBandConfigs(pulseConfigs);

    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;
    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(received, "invalid pulse width for one band still allows other bands to produce a batch");
    for (const auto& sample : firstBatch.samples) {
        test.require(sample.bandIndex != 0,
                     "pulse width greater than or equal to period produces no samples for band");
    }
}

std::map<int, int> firstTargetBeamAmplitudes(TestRunner& test,
                                             double antennaAzimuthDeg,
                                             const std::string& context)
{
    hardware::SimulatorAntennaState state(antennaAzimuthDeg);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 10, 0, 1},
        &state);
    source.setBandConfigs({makeBandConfig(0, 3'000'000'000LL)});
    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;

    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(received, context + ": sample source produces a batch");
    if (!received || firstBatch.samples.empty()) {
        test.require(false, context + ": first batch has visible samples");
        return {};
    }

    const auto firstSampleIndex = firstBatch.samples.front().sampleIndex;
    std::map<int, int> amplitudesByBeam;
    for (const auto& sample : firstBatch.samples) {
        if (sample.sampleIndex != firstSampleIndex) {
            continue;
        }
        amplitudesByBeam[sample.beamIndex] = sample.amplitude;
        test.require(sample.bandIndex == 0, context + ": source maps to configured band");
        test.require(sample.absoluteFrequencyHz == 2'920'000'000LL,
                     context + ": source keeps its absolute frequency");
        test.require(sample.frequencyOffsetHz == -80'000'000LL,
                     context + ": offset is derived from the receiving band center");
    }

    return amplitudesByBeam;
}

void testStartProducesValidBatches(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 32, 0, 1});
    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;

    const bool received = waitForBatch(source, firstBatch, callbackCount);
    const auto stopResult = source.stop();
    const auto callbacksAfterStop = callbackCount.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));

    test.require(received, "sample source produces a batch");
    test.require(stopResult.success, "sample source stops successfully");
    test.require(callbackCount.load() == callbacksAfterStop, "stop prevents further callbacks");
    test.require(!firstBatch.samples.empty(), "sample batch is not empty");

    std::map<int, core::BandConfig> configsByBand;
    for (const auto& config : source.bandConfigs()) {
        configsByBand[config.bandIndex] = config;
    }

    std::uint64_t previousSampleIndex = 0;
    bool hasPrevious = false;
    for (const auto& sample : firstBatch.samples) {
        test.require(!hasPrevious || sample.sampleIndex >= previousSampleIndex,
                     "sampleIndex is monotonic inside batch");
        previousSampleIndex = sample.sampleIndex;
        hasPrevious = true;

        test.require(sample.amplitude >= core::DomainConstraints::minAmplitude
                         && sample.amplitude <= core::DomainConstraints::maxAmplitude,
                     "sample amplitude is in range 1..127");
        test.require(sample.beamIndex == 0 || sample.beamIndex == 1,
                     "sample beamIndex is 0 or 1");

        const auto config = configsByBand.find(sample.bandIndex);
        test.require(config != configsByBand.end(), "sample bandIndex has config");
        if (config != configsByBand.end()) {
            test.require(config->second.containsFrequency(sample.absoluteFrequencyHz),
                         "sample absoluteFrequencyHz is inside band range");
            test.require(sample.absoluteFrequencyHz
                             == config->second.centerFrequencyHz + sample.frequencyOffsetHz,
                         "sample absoluteFrequencyHz matches center plus offset");
            test.require(sample.validate(config->second).isValid(), "sample passes domain validation");
        }
    }
}

void testRejectsInvalidStarts(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 8, 0, 1});

    const auto emptyCallbackResult =
        source.start(hardware::IBcoSampleSource::SampleBatchCallback{});
    test.require(!emptyCallbackResult, "sample source rejects empty callback");

    const auto firstStart = source.start([](const hardware::BcoSampleBatch&) {});
    const auto repeatedStart = source.start([](const hardware::BcoSampleBatch&) {});
    const auto stopResult = source.stop();
    const auto repeatedStopResult = source.stop();

    test.require(firstStart.success, "sample source accepts first start");
    test.require(!repeatedStart, "sample source rejects repeated start");
    test.require(stopResult.success, "sample source stops after start");
    test.require(repeatedStopResult.success, "sample source repeated stop is safe");
}

void testSingleVisibleBeamProducesSamples(TestRunner& test)
{
    const auto amplitudes = firstTargetBeamAmplitudes(test, 75.0, "single visible beam");

    test.require(amplitudes.contains(0), "beam 0 can produce a single-beam waterfall signal");
    test.require(!amplitudes.contains(1), "beam 1 is omitted when below visibility threshold");
}

void testBeamAmplitudesFollowAntennaAzimuth(TestRunner& test)
{
    const auto leftOnly = firstTargetBeamAmplitudes(test, 75.0, "antenna 75 deg");
    const auto centered = firstTargetBeamAmplitudes(test, 45.0, "antenna 45 deg");
    const auto rightOnly = firstTargetBeamAmplitudes(test, 15.0, "antenna 15 deg");

    test.require(leftOnly.contains(0) && !leftOnly.contains(1),
                 "source at 45 deg is visible only in beam 0 when antenna center is 75 deg");
    test.require(centered.contains(0) && centered.contains(1)
                     && std::abs(centered.at(0) - centered.at(1)) <= 1,
                 "source at 45 deg is balanced when antenna center is 45 deg");
    test.require(rightOnly.contains(1) && !rightOnly.contains(0),
                 "source at 45 deg is visible only in beam 1 when antenna center is 15 deg");
}

void testTargetDoesNotFollowMovedBand(TestRunner& test)
{
    hardware::SimulatorAntennaState state(75.0);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 10, 0, 1},
        &state);
    source.setBandConfigs({makeBandConfig(0, 3'000'000'000LL)});
    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;

    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(received, "target is emitted when band covers absolute source frequency");
    for (const auto& sample : firstBatch.samples) {
        test.require(sample.absoluteFrequencyHz == 2'920'000'000LL,
                     "generated sample keeps target absolute frequency");
        test.require(sample.frequencyOffsetHz == -80'000'000LL,
                     "generated sample offset is relative to current band center");
    }

    hardware::SimulatorBcoSampleSource movedSource(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 10, 0, 1},
        &state);
    movedSource.setBandConfigs({makeBandConfig(0, 3'300'000'000LL, 100'000'000LL)});
    hardware::BcoSampleBatch movedBatch;
    std::atomic<int> movedCallbackCount = 0;

    const bool movedReceived = waitForBatch(movedSource, movedBatch, movedCallbackCount);
    movedSource.stop();

    test.require(!movedReceived, "target disappears when band no longer covers its frequency");
    test.require(movedCallbackCount.load() == 0, "moved band produces no callbacks for hidden target");
}

void testTargetHiddenWhenAntennaMisses(TestRunner& test)
{
    hardware::SimulatorAntennaState state(200.0);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 10, 0, 1},
        &state);
    source.setBandConfigs({makeBandConfig(0, 3'000'000'000LL)});
    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;

    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(!received, "target is not emitted when both beams miss by azimuth");
    test.require(callbackCount.load() == 0, "azimuth miss produces no non-empty callback");
}

void testDefaultSceneProducesValidAbsoluteFrequencies(TestRunner& test)
{
    hardware::SimulatorAntennaState state(45.0);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 10, 0, 1},
        &state);
    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;

    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(received, "default radio scene produces a visible batch");
    test.require(!firstBatch.samples.empty(), "default radio scene batch is not empty");
    const auto configs = source.bandConfigs();
    for (const auto& sample : firstBatch.samples) {
        const auto config = std::find_if(configs.begin(),
                                         configs.end(),
                                         [&sample](const auto& item) {
                                             return item.bandIndex == sample.bandIndex;
                                         });
        test.require(config != configs.end(),
                     "default sample has a matching band config");
        test.require(sample.absoluteFrequencyHz
                             == sample.frequencyOffsetHz
                                 + (config != configs.end()
                                        ? config->centerFrequencyHz
                                        : 0),
                     "default sample offset is derived from absolute frequency");
    }
}

} // namespace

int main()
{
    TestRunner test;

    testDefaultPulseConfigsExist(test);
    testPulseMaskRestrictsSamplesToActiveWindow(test);
    testDisabledPulseBandProducesNoSamplesForBand(test);
    testInvalidPulseWidthProducesNoSamplesForBand(test);
    testStartProducesValidBatches(test);
    testRejectsInvalidStarts(test);
    testSingleVisibleBeamProducesSamples(test);
    testBeamAmplitudesFollowAntennaAzimuth(test);
    testTargetDoesNotFollowMovedBand(test);
    testTargetHiddenWhenAntennaMisses(test);
    testDefaultSceneProducesValidAbsoluteFrequencies(test);

    return test.result();
}
