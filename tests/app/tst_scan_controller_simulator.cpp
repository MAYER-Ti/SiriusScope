#include "app/bearingframebus.h"
#include "app/scancontroller.h"
#include "hardware/simulator/simulator_antenna_azimuth_source.h"
#include "hardware/simulator/simulator_antenna_control.h"
#include "hardware/simulator/simulator_antenna_state.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
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

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 2500)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

processing::BearingInputFrame makeBearingFrame()
{
    processing::BearingInputFrame frame;
    frame.bandIndex = 0;
    frame.sampleIndexStart = 1;
    frame.sampleIndexEnd = 1;

    processing::BearingCandidate candidate;
    candidate.bandIndex = 0;
    candidate.sampleIndexStart = 1;
    candidate.sampleIndexEnd = 1;
    candidate.frequencyBin = 0;
    candidate.frequencyRange = core::FrequencyRange{1'000'000'000LL, 1'001'000'000LL};
    candidate.beamAmplitudes = {100, 80};
    candidate.beamPresent = {true, true};
    frame.candidates.push_back(std::move(candidate));

    return frame;
}

void testSimulatorPathCompletesSectorScan(TestRunner& test)
{
    hardware::SimulatorAntennaState state;
    infrastructure::NullDiagnosticsSink diagnostics;
    hardware::SimulatorAntennaControl control(&state, &diagnostics);
    hardware::SimulatorAntennaAzimuthSource source(
        &state,
        hardware::SimulatorAntennaAzimuthSourceConfig{
            std::chrono::milliseconds(5),
            0.0,
            720.0,
        },
        &diagnostics);
    app::BearingFrameBus bus;
    processing::BearingService bearingService;
    app::ScanController controller(&control, &source, &bus, &bearingService, &diagnostics);

    int completedFrames = -1;
    QObject::connect(&controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int frameCount) {
                         completedFrames = frameCount;
                     });

    controller.startScan(10.0, 20.0, 60.0);

    const bool scanning = waitUntil([&controller] {
        return controller.scanStateText() == QStringLiteral("scanning");
    });
    if (scanning) {
        bus.publish({makeBearingFrame()});
    }

    const bool completed = waitUntil([&] {
        return completedFrames == 1;
    });

    test.require(scanning, "simulator path starts sector scan");
    test.require(completed, "simulator path completes sector scan");
    test.require(!controller.scanActive(), "completed simulator scan is inactive");
}

void testSimulatorPathCompletesReverseSectorScan(TestRunner& test)
{
    hardware::SimulatorAntennaState state;
    infrastructure::NullDiagnosticsSink diagnostics;
    hardware::SimulatorAntennaControl control(&state, &diagnostics);
    hardware::SimulatorAntennaAzimuthSource source(
        &state,
        hardware::SimulatorAntennaAzimuthSourceConfig{
            std::chrono::milliseconds(5),
            110.0,
            720.0,
        },
        &diagnostics);
    app::BearingFrameBus bus;
    processing::BearingService bearingService;
    app::ScanController controller(&control, &source, &bus, &bearingService, &diagnostics);

    int completedFrames = -1;
    QObject::connect(&controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int frameCount) {
                         completedFrames = frameCount;
                     });

    waitUntil([&controller] {
        return controller.currentAzimuthDeg() > 109.0;
    });
    controller.startScan(80.0, 100.0, 60.0);

    const bool scanning = waitUntil([&controller] {
        return controller.scanStateText() == QStringLiteral("scanning");
    });
    if (scanning) {
        bus.publish({makeBearingFrame()});
    }

    const bool completed = waitUntil([&] {
        return completedFrames == 1;
    });

    test.require(scanning, "simulator path starts reverse sector scan");
    test.require(completed, "simulator path completes reverse sector scan");
    test.require(!controller.scanActive(), "completed reverse simulator scan is inactive");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testSimulatorPathCompletesSectorScan(test);
    testSimulatorPathCompletesReverseSectorScan(test);

    return test.result();
}
