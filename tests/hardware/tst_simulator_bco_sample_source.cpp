#include "hardware/simulator/simulator_bco_sample_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>

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

} // namespace

int main()
{
    TestRunner test;

    testStartProducesValidBatches(test);
    testRejectsInvalidStarts(test);

    return test.result();
}
