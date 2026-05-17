#include "app/bearingframebus.h"
#include "app/scancontroller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
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

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 1500)
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

void processEventsFor(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

class FakeAntennaControl final : public hardware::IAntennaControl
{
public:
    core::OperationResult moveToAzimuth(double azimuthDeg) override
    {
        ++moveCalls;
        lastMoveAzimuth = azimuthDeg;
        if (failMove) {
            return core::OperationResult::failure("move failed");
        }
        return core::OperationResult::ok();
    }

    core::OperationResult startSectorScan(
        const hardware::AntennaSectorScanCommand& command) override
    {
        ++scanCalls;
        lastScanCommand = command;
        if (failScan) {
            return core::OperationResult::failure("scan failed");
        }
        return core::OperationResult::ok();
    }

    core::OperationResult startManualMove(
        const hardware::AntennaManualMoveCommand& command) override
    {
        ++manualCalls;
        lastManualCommand = command;
        if (failManual) {
            return core::OperationResult::failure("manual failed");
        }
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        ++stopCalls;
        return core::OperationResult::ok();
    }

    int moveCalls = 0;
    int scanCalls = 0;
    int manualCalls = 0;
    int stopCalls = 0;
    double lastMoveAzimuth = 0.0;
    hardware::AntennaSectorScanCommand lastScanCommand;
    hardware::AntennaManualMoveCommand lastManualCommand;
    bool failMove = false;
    bool failScan = false;
    bool failManual = false;
};

class FakeAzimuthSource final : public hardware::IAntennaAzimuthSource
{
public:
    core::OperationResult start(AzimuthCallback callback) override
    {
        ++startCalls;
        m_callback = std::move(callback);
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        ++stopCalls;
        m_callback = {};
        return core::OperationResult::ok();
    }

    void emitAzimuth(double azimuthDeg)
    {
        if (m_callback) {
            m_callback(hardware::AntennaAzimuthSample{
                azimuthDeg,
                std::chrono::system_clock::now(),
            });
        }
    }

    int startCalls = 0;
    int stopCalls = 0;

private:
    AzimuthCallback m_callback;
};

class RecordingDiagnosticsSink final : public infrastructure::IDiagnosticsSink
{
public:
    void publish(const infrastructure::DiagnosticEvent& event) override
    {
        std::lock_guard lock(m_mutex);
        events.push_back(event);
    }

    bool contains(const std::string& text) const
    {
        std::lock_guard lock(m_mutex);
        return std::any_of(events.cbegin(), events.cend(), [&text](const auto& event) {
            return event.message.find(text) != std::string::npos;
        });
    }

private:
    mutable std::mutex m_mutex;
    std::vector<infrastructure::DiagnosticEvent> events;
};

processing::BearingInputFrame makeBearingFrame(std::uint64_t sampleIndex)
{
    processing::BearingInputFrame frame;
    frame.bandIndex = 0;
    frame.sampleIndexStart = sampleIndex;
    frame.sampleIndexEnd = sampleIndex;
    return frame;
}

struct ControllerFixture
{
    FakeAntennaControl control;
    FakeAzimuthSource azimuthSource;
    app::BearingFrameBus bus;
    RecordingDiagnosticsSink diagnostics;
    app::ScanController controller{&control, &azimuthSource, &bus, &diagnostics};
};

void testSelectAndClearSector(TestRunner& test)
{
    ControllerFixture fixture;

    fixture.controller.selectSector(10.0, 60.0);
    test.require(fixture.controller.hasSelectedSector(), "selectSector stores selected sector");
    test.require(fixture.controller.selectedLeftAngle() == 10.0,
                 "selectSector stores left angle");
    test.require(fixture.controller.selectedRightAngle() == 60.0,
                 "selectSector stores right angle");

    fixture.controller.clearSector();
    test.require(!fixture.controller.hasSelectedSector(), "clearSector resets selected sector");
}

void testStartRejectsInvalidAndAlreadyActive(TestRunner& test)
{
    ControllerFixture fixture;

    fixture.controller.startScan(180.0, 220.0, 10.0);
    test.require(fixture.control.moveCalls == 0, "invalid sector does not call antenna control");
    test.require(!fixture.controller.lastError().isEmpty(), "invalid sector stores last error");

    fixture.controller.startScan(10.0, 60.0, 10.0);
    fixture.controller.startScan(20.0, 80.0, 10.0);
    test.require(fixture.control.moveCalls == 1, "already-active scan is rejected");
    test.require(fixture.controller.scanActive(), "first scan remains active");
}

void testStartStopAndManualCommands(TestRunner& test)
{
    ControllerFixture fixture;

    fixture.controller.driveLeft(10.0);
    test.require(fixture.control.manualCalls == 1, "driveLeft calls IAntennaControl");
    test.require(fixture.control.lastManualCommand.direction
                     == hardware::AntennaManualMoveCommand::Direction::Left,
                 "driveLeft sends left direction");

    fixture.controller.startScan(10.0, 60.0, 10.0);
    test.require(fixture.control.moveCalls == 1, "startScan calls moveToAzimuth");
    test.require(fixture.control.lastMoveAzimuth == 10.0, "startScan moves to planned start");

    fixture.controller.driveRight(10.0);
    test.require(fixture.control.manualCalls == 1,
                 "manual movement is disabled while scan is active");

    fixture.controller.stopScan();
    test.require(fixture.control.stopCalls >= 1, "stopScan calls IAntennaControl::stop");
    test.require(!fixture.controller.scanActive(), "stopScan clears active scan");
}

void testAzimuthProgressCompletionAndFrames(TestRunner& test)
{
    ControllerFixture fixture;
    int completedFrames = -1;
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int frameCount) {
                         completedFrames = frameCount;
                     });

    fixture.controller.startScan(10.0, 60.0, 10.0);
    fixture.azimuthSource.emitAzimuth(10.0);
    const bool scanStarted = waitUntil([&fixture] {
        return fixture.control.scanCalls == 1;
    });

    fixture.bus.publish({makeBearingFrame(1)});
    fixture.azimuthSource.emitAzimuth(35.0);
    const bool progressUpdated = waitUntil([&fixture] {
        return fixture.controller.scanProgress() > 0.4;
    });

    fixture.azimuthSource.emitAzimuth(60.0);
    const bool completed = waitUntil([&] {
        return completedFrames == 1;
    });

    test.require(scanStarted, "azimuth at start launches sector scan command");
    test.require(progressUpdated, "scan progress grows during movement");
    test.require(completed, "scan completes when target is reached");
    test.require(!fixture.controller.scanActive(), "completed scan is no longer active");
}

void testSpeedChangeRejectedDuringScan(TestRunner& test)
{
    ControllerFixture fixture;
    fixture.controller.setScanSpeedDegPerSec(12.0);
    fixture.controller.startSelectedSectorScan(12.0);
    test.require(!fixture.controller.scanActive(),
                 "startSelectedSectorScan without sector is rejected");

    fixture.controller.selectSector(10.0, 60.0);
    fixture.controller.startSelectedSectorScan(12.0);
    fixture.controller.setScanSpeedDegPerSec(20.0);

    test.require(fixture.controller.scanSpeedDegPerSec() == 12.0,
                 "scan speed cannot change while scan is active");
}

void testDiagnosticsOnFailure(TestRunner& test)
{
    ControllerFixture fixture;
    fixture.control.failMove = true;

    fixture.controller.startScan(10.0, 60.0, 10.0);

    test.require(!fixture.controller.scanActive(), "failed start does not leave active scan");
    test.require(fixture.diagnostics.contains("antenna command failed"),
                 "scan failure is published to diagnostics");
}

void testAzimuthSampleUpdatesCurrentValue(TestRunner& test)
{
    ControllerFixture fixture;

    fixture.azimuthSource.emitAzimuth(42.5);
    const bool updated = waitUntil([&fixture] {
        return std::abs(fixture.controller.currentAzimuthDeg() - 42.5) < 0.001;
    });

    test.require(updated, "azimuth samples update currentAzimuthDeg");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testSelectAndClearSector(test);
    testStartRejectsInvalidAndAlreadyActive(test);
    testStartStopAndManualCommands(test);
    testAzimuthProgressCompletionAndFrames(test);
    testSpeedChangeRejectedDuringScan(test);
    testDiagnosticsOnFailure(test);
    testAzimuthSampleUpdatesCurrentValue(test);

    return test.result();
}
