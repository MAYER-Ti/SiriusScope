#include "hardware/simulator/simulator_bco_sample_source.h"
#include "hardware/simulator/simulator_antenna_state.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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

std::pair<int, int> firstSourceAmplitudes(TestRunner& test,
                                          double antennaAzimuthDeg,
                                          const std::string& context)
{
    hardware::SimulatorAntennaState state(antennaAzimuthDeg);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 10, 0, 1},
        &state);
    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;

    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    test.require(received, context + ": sample source produces a batch");
    if (!received || firstBatch.samples.size() < 2) {
        test.require(false, context + ": first batch has at least one complete beam pair");
        return {0, 0};
    }

    const auto& first = firstBatch.samples[0];
    const auto& second = firstBatch.samples[1];
    test.require(first.bandIndex == 0 && second.bandIndex == 0,
                 context + ": first generated source belongs to band 0");
    test.require(first.sampleIndex == second.sampleIndex,
                 context + ": beam pair uses one sampleIndex");
    test.require(first.frequencyOffsetHz == second.frequencyOffsetHz,
                 context + ": beam pair uses one frequency offset");

    const core::SignalSample* beam0 = nullptr;
    const core::SignalSample* beam1 = nullptr;
    for (const auto* sample : {&first, &second}) {
        if (sample->beamIndex == 0) {
            beam0 = sample;
        } else if (sample->beamIndex == 1) {
            beam1 = sample;
        }
    }

    test.require(beam0 != nullptr && beam1 != nullptr,
                 context + ": first source contains beam 0 and beam 1");
    if (!beam0 || !beam1) {
        return {0, 0};
    }

    return {beam0->amplitude, beam1->amplitude};
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

void testFirstGeneratedSourceUsesCompleteBeamPair(TestRunner& test)
{
    const auto amplitudes = firstSourceAmplitudes(test, 75.0, "first generated source");

    test.require(amplitudes.first >= core::DomainConstraints::minAmplitude
                     && amplitudes.second >= core::DomainConstraints::minAmplitude,
                 "first generated source emits valid amplitudes for both beams");
}

void testBeamAmplitudesFollowAntennaAzimuth(TestRunner& test)
{
    const auto leftDominant = firstSourceAmplitudes(test, 75.0, "antenna 75 deg");
    const auto centered = firstSourceAmplitudes(test, 45.0, "antenna 45 deg");
    const auto rightDominant = firstSourceAmplitudes(test, 15.0, "antenna 15 deg");

    test.require(leftDominant.first > leftDominant.second,
                 "source at 45 deg is beam 0 dominant when antenna center is 75 deg");
    test.require(std::abs(centered.first - centered.second) <= 1,
                 "source at 45 deg is balanced when antenna center is 45 deg");
    test.require(rightDominant.second > rightDominant.first,
                 "source at 45 deg is beam 1 dominant when antenna center is 15 deg");
}

void testDefaultSceneProducesAllConfiguredBands(TestRunner& test)
{
    hardware::SimulatorAntennaState state(45.0);
    hardware::SimulatorBcoSampleSource source(
        hardware::SimulatorBcoSampleSourceConfig{std::chrono::milliseconds(10), 10, 0, 1},
        &state);
    hardware::BcoSampleBatch firstBatch;
    std::atomic<int> callbackCount = 0;

    const bool received = waitForBatch(source, firstBatch, callbackCount);
    source.stop();

    std::set<int> bands;
    for (const auto& sample : firstBatch.samples) {
        bands.insert(sample.bandIndex);
    }

    test.require(received, "default radio scene produces a batch");
    for (int bandIndex = 0; bandIndex < core::DomainConstraints::currentBandCount; ++bandIndex) {
        test.require(bands.contains(bandIndex),
                     "default radio scene produces samples for every configured band");
    }
}

} // namespace

int main()
{
    TestRunner test;

    testStartProducesValidBatches(test);
    testRejectsInvalidStarts(test);
    testFirstGeneratedSourceUsesCompleteBeamPair(test);
    testBeamAmplitudesFollowAntennaAzimuth(test);
    testDefaultSceneProducesAllConfiguredBands(test);

    return test.result();
}
