#include "hardware/simulator/simulator_antenna_azimuth_source.h"
#include "hardware/simulator/simulator_antenna_control.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

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

bool isInBlindZone(double azimuthDeg)
{
    return azimuthDeg > 170.0 && azimuthDeg < 190.0;
}

void testAzimuthSourceStartStop(TestRunner& test)
{
    hardware::SimulatorAntennaState state;
    hardware::SimulatorAntennaAzimuthSource source(
        &state,
        hardware::SimulatorAntennaAzimuthSourceConfig{
            std::chrono::milliseconds(10),
            0.0,
            90.0,
        });

    std::mutex mutex;
    std::condition_variable condition;
    hardware::AntennaAzimuthSample firstSample;
    bool received = false;

    const auto emptyCallbackResult =
        source.start(hardware::IAntennaAzimuthSource::AzimuthCallback{});
    test.require(!emptyCallbackResult, "azimuth source rejects empty callback");

    const auto startResult = source.start([&](const hardware::AntennaAzimuthSample& sample) {
        {
            std::lock_guard lock(mutex);
            if (!received) {
                firstSample = sample;
                received = true;
            }
        }
        condition.notify_one();
    });
    const auto repeatedStartResult = source.start([](const hardware::AntennaAzimuthSample&) {});

    std::unique_lock lock(mutex);
    const bool gotSample =
        condition.wait_for(lock, std::chrono::milliseconds(500), [&] { return received; });
    lock.unlock();

    const auto stopResult = source.stop();
    const auto repeatedStopResult = source.stop();

    test.require(startResult.success, "azimuth source accepts first start");
    test.require(!repeatedStartResult, "azimuth source rejects repeated start");
    test.require(gotSample, "azimuth source publishes sample");
    test.require(firstSample.timestamp != std::chrono::system_clock::time_point{},
                 "azimuth sample timestamp is filled");
    test.require(firstSample.degrees >= 0.0 && firstSample.degrees < 360.0,
                 "azimuth sample stays in [0, 360)");
    test.require(stopResult.success, "azimuth source stops");
    test.require(repeatedStopResult.success, "azimuth source repeated stop is safe");
}

void testAntennaControlCommands(TestRunner& test)
{
    hardware::SimulatorAntennaState state;
    hardware::SimulatorAntennaControl control(&state);

    const auto moveResult = control.moveToAzimuth(90.0);
    test.require(moveResult.success, "moveToAzimuth accepts valid target");
    test.require(state.targetAzimuthDeg() == 90.0, "moveToAzimuth stores target azimuth");
    test.require(state.isMoving(), "moveToAzimuth marks antenna moving");

    const auto blindMoveResult = control.moveToAzimuth(180.0);
    test.require(!blindMoveResult, "moveToAzimuth rejects blind zone target");

    const auto validSector = core::ScanSector::create(20.0, 100.0);
    test.require(validSector.hasValue(), "test creates valid scan sector");
    const auto sectorResult = control.startSectorScan(*validSector.value());
    test.require(sectorResult.success, "startSectorScan accepts valid sector");
    test.require(state.activeScanSector().has_value(), "startSectorScan stores active sector");
    test.require(state.currentAzimuthDeg() == 20.0, "startSectorScan sets current to sector start");
    test.require(state.targetAzimuthDeg() == 100.0, "startSectorScan sets target to sector end");

    const auto crossingSector = core::ScanSector::create(150.0, 200.0);
    test.require(crossingSector.hasValue(), "test creates blind-zone crossing sector");
    const auto crossingResult = control.startSectorScan(*crossingSector.value());
    test.require(!crossingResult, "startSectorScan rejects sector crossing blind zone");

    const auto stopResult = control.stop();
    const auto repeatedStopResult = control.stop();
    test.require(stopResult.success, "antenna control stop succeeds");
    test.require(repeatedStopResult.success, "antenna control repeated stop succeeds");
    test.require(!state.isMoving(), "antenna control stop clears movement");
    test.require(!state.activeScanSector().has_value(), "antenna control stop clears sector");
}

void testAzimuthMovementStaysValid(TestRunner& test)
{
    hardware::SimulatorAntennaState state;
    hardware::SimulatorAntennaControl control(&state);
    hardware::SimulatorAntennaAzimuthSource source(
        &state,
        hardware::SimulatorAntennaAzimuthSourceConfig{
            std::chrono::milliseconds(10),
            0.0,
            720.0,
        });

    const auto moveResult = control.moveToAzimuth(200.0);
    test.require(moveResult.success, "moveToAzimuth accepts target outside blind zone");

    std::mutex mutex;
    std::condition_variable condition;
    int sampleCount = 0;
    bool allInRange = true;
    bool noBlindZoneSamples = true;

    const auto startResult = source.start([&](const hardware::AntennaAzimuthSample& sample) {
        std::lock_guard lock(mutex);
        ++sampleCount;
        allInRange = allInRange && sample.degrees >= 0.0 && sample.degrees < 360.0;
        noBlindZoneSamples = noBlindZoneSamples && !isInBlindZone(sample.degrees);
        condition.notify_one();
    });

    std::unique_lock lock(mutex);
    const bool gotSamples =
        condition.wait_for(lock, std::chrono::milliseconds(500), [&] { return sampleCount >= 5; });
    lock.unlock();

    source.stop();

    test.require(startResult.success, "azimuth source starts for movement test");
    test.require(gotSamples, "azimuth source publishes movement samples");
    test.require(allInRange, "movement samples stay in [0, 360)");
    test.require(noBlindZoneSamples, "movement samples avoid blind zone");
}

} // namespace

int main()
{
    TestRunner test;

    testAzimuthSourceStartStop(test);
    testAntennaControlCommands(test);
    testAzimuthMovementStaysValid(test);

    return test.result();
}
