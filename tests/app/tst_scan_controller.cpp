#include "app/bearingframebus.h"
#include "app/interfaces/processing_flush_control.h"
#include "app/interfaces/result_table_sink.h"
#include "app/interfaces/scan_acquisition_recorder.h"
#include "app/interfaces/scan_recording_control.h"
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
#include <optional>
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

bool containsEventsInOrder(const std::vector<std::string>& events,
                           const std::vector<std::string>& expected)
{
    auto current = events.cbegin();
    for (const auto& item : expected) {
        current = std::find(current, events.cend(), item);
        if (current == events.cend()) {
            return false;
        }
        ++current;
    }
    return true;
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

class RecordingScanAcquisitionRecorder final : public app::IScanAcquisitionRecorder
{
public:
    explicit RecordingScanAcquisitionRecorder(std::vector<std::string>* eventLog = nullptr)
        : events(eventLog)
    {
    }

    core::OperationResult begin(const app::ScanAcquisitionMetadata& metadata) override
    {
        if (activeSession) {
            return core::OperationResult::failure("active");
        }
        if (failBegin) {
            return core::OperationResult::failure("begin failed");
        }

        ++beginCalls;
        if (events) {
            events->push_back("acquisition.begin");
        }
        activeSession = app::ScanAcquisitionSession{metadata, {}};
        return core::OperationResult::ok();
    }

    core::OperationResult append(
        const processing::BearingFrameObservation& observation) override
    {
        if (!activeSession) {
            return core::OperationResult::failure("inactive");
        }
        if (failAppend) {
            return core::OperationResult::failure("append failed");
        }

        ++appendCalls;
        if (events) {
            events->push_back("acquisition.append");
        }
        activeSession->observations.push_back(observation);
        return core::OperationResult::ok();
    }

    core::OperationResult close(const app::ScanAcquisitionMetadata& finalMetadata) override
    {
        if (!activeSession) {
            return core::OperationResult::failure("inactive");
        }

        ++closeCalls;
        if (events) {
            events->push_back("acquisition.close");
        }
        activeSession->metadata = finalMetadata;
        closedSessions.push_back(std::move(*activeSession));
        activeSession.reset();
        return core::OperationResult::ok();
    }

    std::vector<processing::BearingFrameObservation> observations(
        std::uint64_t scanSessionId) const override
    {
        if (activeSession && activeSession->metadata.scanSessionId == scanSessionId) {
            return activeSession->observations;
        }
        const auto found =
            std::find_if(closedSessions.cbegin(),
                         closedSessions.cend(),
                         [scanSessionId](const app::ScanAcquisitionSession& session) {
                             return session.metadata.scanSessionId == scanSessionId;
                         });
        return found == closedSessions.cend()
            ? std::vector<processing::BearingFrameObservation>{}
            : found->observations;
    }

    bool active() const noexcept override { return activeSession.has_value(); }

    int beginCalls = 0;
    int appendCalls = 0;
    int closeCalls = 0;
    bool failBegin = false;
    bool failAppend = false;
    std::optional<app::ScanAcquisitionSession> activeSession;
    std::vector<app::ScanAcquisitionSession> closedSessions;
    std::vector<std::string>* events = nullptr;
};

class FakeProcessingFlushControl final : public app::IProcessingFlushControl
{
public:
    explicit FakeProcessingFlushControl(std::vector<std::string>* eventLog = nullptr)
        : events(eventLog)
    {
    }

    core::OperationResult flushProcessing(std::chrono::milliseconds) override
    {
        ++flushCalls;
        if (events) {
            events->push_back("processing.flush");
        }
        if (onFlush) {
            onFlush();
        }
        if (failFlush) {
            return core::OperationResult::failure("flush failed");
        }
        return core::OperationResult::ok();
    }

    void flushProcessingAsync(std::chrono::milliseconds, FlushCallback callback) override
    {
        ++flushCalls;
        if (events) {
            events->push_back("processing.flush");
        }
        if (onFlush) {
            onFlush();
        }
        if (failFlush) {
            callback(core::OperationResult::failure("flush failed"));
            return;
        }
        if (deferAsyncCallback) {
            pendingCallback = std::move(callback);
            return;
        }
        callback(core::OperationResult::ok());
    }

    void completePendingFlush(core::OperationResult result = core::OperationResult::ok())
    {
        if (!pendingCallback) {
            return;
        }

        auto callback = std::move(pendingCallback);
        pendingCallback = nullptr;
        callback(std::move(result));
    }

    int flushCalls = 0;
    bool failFlush = false;
    bool deferAsyncCallback = false;
    std::function<void()> onFlush;
    FlushCallback pendingCallback;
    std::vector<std::string>* events = nullptr;
};

class FakeScanRecordingControl final : public app::IScanRecordingControl
{
public:
    explicit FakeScanRecordingControl(std::vector<std::string>* eventLog = nullptr)
        : events(eventLog)
    {
    }

    core::OperationResult beginScanRecording(std::uint64_t scanSessionId) override
    {
        ++beginCalls;
        activeSessionId = scanSessionId;
        if (events) {
            events->push_back("recording.begin");
        }
        return core::OperationResult::ok();
    }

    core::OperationResult endScanRecording(std::uint64_t scanSessionId) override
    {
        ++endCalls;
        endedSessionId = scanSessionId;
        activeSessionId.reset();
        if (events) {
            events->push_back("recording.end");
        }
        return core::OperationResult::ok();
    }

    int beginCalls = 0;
    int endCalls = 0;
    std::optional<std::uint64_t> activeSessionId;
    std::uint64_t endedSessionId = 0;
    std::vector<std::string>* events = nullptr;
};

class RecordingResultTableSink final : public app::IResultTableSink
{
public:
    explicit RecordingResultTableSink(std::vector<std::string>* eventLog = nullptr)
        : events(eventLog)
    {
    }

    core::OperationResult appendBearingResults(
        const app::ResultTableAppendContext& context,
        const std::vector<core::BearingResult>& results) override
    {
        ++appendCalls;
        lastSessionId = context.scanSessionId;
        lastAntennaAzimuthDeg = context.antennaAzimuthDeg;
        lastResultCount = static_cast<int>(results.size());
        if (events) {
            events->push_back("result.append");
        }
        return core::OperationResult::ok();
    }

    int appendCalls = 0;
    int lastResultCount = 0;
    std::uint64_t lastSessionId = 0;
    double lastAntennaAzimuthDeg = 0.0;
    std::vector<std::string>* events = nullptr;
};

class RecordingBearingService final : public processing::BearingService
{
public:
    explicit RecordingBearingService(std::vector<std::string>* eventLog = nullptr)
        : events(eventLog)
    {
    }

    processing::BearingCalculationResult calculate(
        const std::vector<processing::BearingFrameObservation>& observations,
        const core::TimeBase& timeBase,
        const core::RuntimeCapabilities& capabilities =
            core::defaultRuntimeCapabilities()) const override
    {
        if (events) {
            events->push_back("bearing.calculate");
        }
        return processing::BearingService::calculate(observations, timeBase, capabilities);
    }

    std::vector<std::string>* events = nullptr;
};

processing::BearingInputFrame makeBearingFrame(std::uint64_t sampleIndex)
{
    processing::BearingInputFrame frame;
    frame.bandIndex = 0;
    frame.sampleIndexStart = sampleIndex;
    frame.sampleIndexEnd = sampleIndex;

    processing::BearingCandidate candidate;
    candidate.bandIndex = 0;
    candidate.sampleIndexStart = sampleIndex;
    candidate.sampleIndexEnd = sampleIndex;
    candidate.frequencyBin = 0;
    candidate.frequencyRange = core::FrequencyRange{1'000'000'000LL, 1'001'000'000LL};
    candidate.beamAmplitudes = {100, 80};
    candidate.beamPresent = {true, true};
    frame.candidates.push_back(std::move(candidate));

    return frame;
}

struct ControllerFixture
{
    std::vector<std::string> events;
    FakeAntennaControl control;
    FakeAzimuthSource azimuthSource;
    app::BearingFrameBus bus;
    RecordingBearingService bearingService{&events};
    RecordingScanAcquisitionRecorder recorder{&events};
    FakeProcessingFlushControl flushControl{&events};
    FakeScanRecordingControl recordingControl{&events};
    RecordingResultTableSink resultTableSink{&events};
    RecordingDiagnosticsSink diagnostics;
    app::ScanController controller{&control,
                                   &azimuthSource,
                                   &bus,
                                   &bearingService,
                                   &recorder,
                                   &flushControl,
                                   &recordingControl,
                                   &resultTableSink,
                                   &diagnostics};
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

    fixture.controller.setAntennaSpeedDegPerSec(15.0);
    fixture.controller.driveLeft();
    test.require(fixture.control.manualCalls == 1, "driveLeft calls IAntennaControl");
    test.require(fixture.control.lastManualCommand.direction
                     == hardware::AntennaManualMoveCommand::Direction::Left,
                 "driveLeft sends left direction");
    test.require(fixture.control.lastManualCommand.speedDegPerSec == 15.0,
                 "driveLeft uses configured antenna speed");

    fixture.controller.setAntennaSpeedDegPerSec(20.0);
    fixture.controller.driveRight();
    test.require(fixture.control.manualCalls == 2, "driveRight calls IAntennaControl");
    test.require(fixture.control.lastManualCommand.direction
                     == hardware::AntennaManualMoveCommand::Direction::Right,
                 "driveRight sends right direction");
    test.require(fixture.control.lastManualCommand.speedDegPerSec == 20.0,
                 "driveRight uses configured antenna speed");

    fixture.controller.startScan(10.0, 60.0, 10.0);
    test.require(fixture.control.moveCalls == 1, "startScan calls moveToAzimuth");
    test.require(fixture.control.lastMoveAzimuth == 10.0, "startScan moves to planned start");

    fixture.controller.driveRight();
    test.require(fixture.control.manualCalls == 2,
                 "manual movement is disabled while scan is active");

    fixture.controller.stopScan();
    test.require(fixture.control.stopCalls >= 1, "stopScan calls IAntennaControl::stop");
    test.require(!fixture.controller.scanActive(), "stopScan clears active scan");
}

void testCommonAntennaSpeedUsedBySelectedScan(TestRunner& test)
{
    ControllerFixture fixture;

    fixture.controller.selectSector(10.0, 60.0);
    fixture.controller.setAntennaSpeedDegPerSec(12.0);
    fixture.controller.startSelectedSectorScan();
    fixture.azimuthSource.emitAzimuth(10.0);

    const bool scanStarted = waitUntil([&fixture] {
        return fixture.control.scanCalls == 1;
    });

    test.require(scanStarted, "startSelectedSectorScan starts scan");
    test.require(fixture.control.lastScanCommand.speedDegPerSec == 12.0,
                 "startSelectedSectorScan uses configured antenna speed");
}

void testAcquisitionOpensOnlyWhenSectorScanBegins(TestRunner& test)
{
    ControllerFixture fixture;

    fixture.controller.startScan(10.0, 60.0, 10.0);
    processEventsFor(40);

    test.require(fixture.controller.scanStateText() == QStringLiteral("moving to start"),
                 "startScan enters MovingToStart first");
    test.require(fixture.recorder.beginCalls == 0,
                 "startScan does not open acquisition while moving to start");
    test.require(fixture.recordingControl.beginCalls == 0,
                 "startScan does not start scan recording while moving to start");

    fixture.azimuthSource.emitAzimuth(10.0);
    const bool opened = waitUntil([&fixture] {
        return fixture.recorder.beginCalls == 1
            && fixture.controller.scanStateText() == QStringLiteral("scanning");
    });

    test.require(opened, "beginSectorScan opens acquisition");
    test.require(fixture.recordingControl.beginCalls == 1,
                 "beginSectorScan starts waterfall scan recording");
}

void testBearingFramesOutsideScanIgnored(TestRunner& test)
{
    ControllerFixture fixture;

    fixture.bus.publish({makeBearingFrame(1)});
    processEventsFor(40);

    test.require(fixture.controller.targetAzimuthsDeg().empty(),
                 "bearing frames outside scanning do not create target azimuths");
    test.require(fixture.controller.targetBearings().empty(),
                 "bearing frames outside scanning do not create target bearings");
    test.require(fixture.recorder.appendCalls == 0,
                 "bearing frames outside scanning do not enter acquisition");
}

void testFlushDrainFramesBeforeClose(TestRunner& test)
{
    ControllerFixture fixture;
    int completedFrames = -1;
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int frameCount) {
                         completedFrames = frameCount;
                     });

    fixture.flushControl.onFlush = [&fixture] {
        fixture.bus.publish({makeBearingFrame(77)});
    };

    fixture.controller.startScan(10.0, 60.0, 10.0);
    fixture.azimuthSource.emitAzimuth(10.0);
    const bool scanStarted = waitUntil([&fixture] {
        return fixture.recorder.beginCalls == 1;
    });

    fixture.azimuthSource.emitAzimuth(60.0);
    const bool completed = waitUntil([&] {
        return completedFrames == 1;
    });

    test.require(scanStarted, "scan starts before flush-drain test");
    test.require(completed, "scan completes after flush-drained frame");
    test.require(fixture.flushControl.flushCalls == 1,
                 "completeScan asks processing path to flush");
    test.require(fixture.recorder.closeCalls == 1,
                 "completeScan closes acquisition");
    test.require(fixture.resultTableSink.appendCalls == 1,
                 "calculated results are forwarded to result table sink");
    test.require(std::abs(fixture.resultTableSink.lastAntennaAzimuthDeg - 60.0) < 0.001,
                 "result table context receives final antenna azimuth");
}

void testCompletionWaitsForAsyncFlushCallback(TestRunner& test)
{
    ControllerFixture fixture;
    fixture.flushControl.deferAsyncCallback = true;
    int completedFrames = -1;
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int frameCount) {
                         completedFrames = frameCount;
                     });

    fixture.controller.startScan(10.0, 60.0, 10.0);
    fixture.azimuthSource.emitAzimuth(10.0);
    const bool scanStarted = waitUntil([&fixture] {
        return fixture.recorder.beginCalls == 1;
    });

    fixture.azimuthSource.emitAzimuth(60.0);
    const bool completing = waitUntil([&fixture] {
        return fixture.controller.scanStateText() == QStringLiteral("completing");
    });

    test.require(scanStarted, "scan starts before async completion test");
    test.require(completing, "scan enters Completing while async flush is pending");
    test.require(fixture.recorder.closeCalls == 0,
                 "scan acquisition remains open until async flush callback");
    test.require(completedFrames == -1,
                 "scanCompleted is not emitted before async flush callback");

    fixture.flushControl.completePendingFlush();
    const bool completed = waitUntil([&] {
        return completedFrames == 0;
    });

    test.require(completed, "scan completes after async flush callback");
    test.require(fixture.recorder.closeCalls == 1,
                 "scan acquisition closes after async flush callback");
}

void testStopScanClosesAcquisitionAndRecording(TestRunner& test)
{
    ControllerFixture fixture;
    int cancelledSessionId = -1;
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCancelled,
                     [&](qulonglong sessionId) {
                         cancelledSessionId = static_cast<int>(sessionId);
                     });

    fixture.controller.startScan(10.0, 60.0, 10.0);
    fixture.azimuthSource.emitAzimuth(10.0);
    const bool opened = waitUntil([&fixture] {
        return fixture.recorder.beginCalls == 1;
    });

    fixture.bus.publish({makeBearingFrame(1)});
    waitUntil([&fixture] {
        return fixture.recorder.appendCalls == 1;
    });

    fixture.controller.stopScan();

    test.require(opened, "scan acquisition opened before cancellation");
    test.require(cancelledSessionId == 1, "stopScan emits cancelled session id");
    test.require(fixture.recorder.closeCalls == 1,
                 "stopScan closes active acquisition");
    test.require(fixture.recordingControl.endCalls == 1,
                 "stopScan ends scan recording");
    test.require(fixture.resultTableSink.appendCalls == 0,
                 "cancelled scan does not append bearing results");
}

void testAzimuthProgressCompletionAndFrames(TestRunner& test)
{
    ControllerFixture fixture;
    int completedFrames = -1;
    int bearingResultCount = -1;
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int frameCount) {
                         completedFrames = frameCount;
                     });
    QObject::connect(&fixture.controller,
                     &app::ScanController::bearingResultsReady,
                     [&](qulonglong, int resultCount) {
                         bearingResultCount = resultCount;
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
    test.require(bearingResultCount == 1, "scan with bearing frame emits one result");
    test.require(fixture.recorder.appendCalls == 1,
                 "bearing frame during Scanning is appended to acquisition");
    test.require(fixture.recorder.closeCalls == 1,
                 "completed scan closes acquisition before calculation result is emitted");
    test.require(fixture.controller.targetAzimuthsDeg().size() == 1,
                 "targetAzimuthsDeg exposes calculated bearing");
    test.require(fixture.controller.targetBearings().size() == 1,
                 "targetBearings exposes calculated bearing details");
    test.require(!fixture.controller.scanActive(), "completed scan is no longer active");
}

void testReverseScanProgressAndCompletion(TestRunner& test)
{
    ControllerFixture fixture;
    int completedFrames = -1;
    int bearingResultCount = -1;
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int frameCount) {
                         completedFrames = frameCount;
                     });
    QObject::connect(&fixture.controller,
                     &app::ScanController::bearingResultsReady,
                     [&](qulonglong, int resultCount) {
                         bearingResultCount = resultCount;
                     });

    fixture.azimuthSource.emitAzimuth(110.0);
    waitUntil([&fixture] {
        return std::abs(fixture.controller.currentAzimuthDeg() - 110.0) < 0.001;
    });

    fixture.controller.startScan(40.0, 100.0, 10.0);
    test.require(fixture.control.lastMoveAzimuth == 100.0,
                 "scan near right side moves to right edge first");

    fixture.azimuthSource.emitAzimuth(100.0);
    const bool scanStarted = waitUntil([&fixture] {
        return fixture.control.scanCalls == 1;
    });
    test.require(scanStarted, "reverse scan starts at right edge");
    test.require(fixture.control.lastScanCommand.direction
                     == core::ScanDirection::DecreasingSafeCoord,
                 "reverse scan command carries decreasing direction");

    fixture.azimuthSource.emitAzimuth(70.0);
    const bool progressUpdated = waitUntil([&fixture] {
        return fixture.controller.scanProgress() > 0.4;
    });

    fixture.azimuthSource.emitAzimuth(40.0);
    const bool completed = waitUntil([&] {
        return completedFrames == 0;
    });

    test.require(progressUpdated, "reverse scan progress grows during movement");
    test.require(completed, "reverse scan completes when left edge is reached");
    test.require(bearingResultCount == 0, "empty scan emits zero bearing results");
    test.require(fixture.controller.targetAzimuthsDeg().empty(),
                 "empty scan has no target azimuths");
    test.require(fixture.controller.targetBearings().empty(),
                 "empty scan has no target bearing details");
    test.require(fixture.diagnostics.contains("scan acquisition has no observations"),
                 "empty scan acquisition is diagnosed");
}

void testRepeatScanClearsOldBearingResults(TestRunner& test)
{
    ControllerFixture fixture;
    int completedCount = 0;
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int) {
                         ++completedCount;
                     });

    fixture.controller.startScan(10.0, 60.0, 10.0);
    fixture.azimuthSource.emitAzimuth(10.0);
    const bool firstScanStarted = waitUntil([&fixture] {
        return fixture.control.scanCalls == 1;
    });
    if (firstScanStarted) {
        fixture.bus.publish({makeBearingFrame(1)});
    }
    fixture.azimuthSource.emitAzimuth(60.0);
    const bool firstCompleted = waitUntil([&] {
        return completedCount == 1;
    });

    test.require(firstCompleted && fixture.controller.targetBearings().size() == 1,
                 "first scan produces a bearing result");

    fixture.controller.startScan(70.0, 100.0, 10.0);
    test.require(fixture.controller.targetBearings().empty(),
                 "starting the next scan clears old bearing details");
    test.require(fixture.controller.targetAzimuthsDeg().empty(),
                 "starting the next scan clears old target azimuths");

    fixture.azimuthSource.emitAzimuth(70.0);
    const bool secondScanStarted = waitUntil([&fixture] {
        return fixture.control.scanCalls == 2;
    });
    fixture.azimuthSource.emitAzimuth(100.0);
    const bool secondCompleted = waitUntil([&] {
        return completedCount == 2;
    });

    test.require(secondScanStarted, "second scan starts");
    test.require(secondCompleted, "second scan completes");
    test.require(fixture.controller.targetBearings().empty(),
                 "empty second scan keeps bearing details empty");
}

void testScanLifecycleEventOrder(TestRunner& test)
{
    ControllerFixture fixture;
    QObject::connect(&fixture.controller,
                     &app::ScanController::bearingResultsReady,
                     [&](qulonglong, int) {
                         fixture.events.push_back("bearing.resultsReady");
                     });
    QObject::connect(&fixture.controller,
                     &app::ScanController::scanCompleted,
                     [&](qulonglong, int) {
                         fixture.events.push_back("scan.completed");
                     });

    fixture.controller.startScan(10.0, 60.0, 10.0);
    fixture.azimuthSource.emitAzimuth(10.0);
    const bool opened = waitUntil([&fixture] {
        return fixture.recorder.beginCalls == 1;
    });
    if (opened) {
        fixture.bus.publish({makeBearingFrame(1)});
    }
    const bool appended = waitUntil([&fixture] {
        return fixture.recorder.appendCalls == 1;
    });

    fixture.azimuthSource.emitAzimuth(60.0);
    const bool completed = waitUntil([&fixture] {
        return containsEventsInOrder(fixture.events,
                                     {"scan.completed"});
    });

    test.require(opened, "lifecycle order test opens acquisition");
    test.require(appended, "lifecycle order test appends observation");
    test.require(completed, "lifecycle order test completes scan");
    test.require(containsEventsInOrder(fixture.events,
                                       {"acquisition.begin",
                                        "acquisition.append",
                                        "processing.flush",
                                        "recording.end",
                                        "acquisition.close",
                                        "bearing.calculate",
                                        "bearing.resultsReady",
                                        "result.append",
                                        "scan.completed"}),
                 "scan lifecycle events occur in the expected order");
}

void testSpeedChangeRejectedDuringScan(TestRunner& test)
{
    ControllerFixture fixture;
    fixture.controller.setAntennaSpeedDegPerSec(12.0);
    fixture.controller.startSelectedSectorScan();
    test.require(!fixture.controller.scanActive(),
                 "startSelectedSectorScan without sector is rejected");

    fixture.controller.selectSector(10.0, 60.0);
    fixture.controller.startSelectedSectorScan();
    fixture.controller.setAntennaSpeedDegPerSec(20.0);

    test.require(fixture.controller.antennaSpeedDegPerSec() == 12.0,
                 "antenna speed cannot change while scan is active");
    test.require(fixture.controller.scanSpeedDegPerSec() == 12.0,
                 "legacy scan speed alias keeps antenna speed");
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
    testCommonAntennaSpeedUsedBySelectedScan(test);
    testAcquisitionOpensOnlyWhenSectorScanBegins(test);
    testBearingFramesOutsideScanIgnored(test);
    testFlushDrainFramesBeforeClose(test);
    testCompletionWaitsForAsyncFlushCallback(test);
    testStopScanClosesAcquisitionAndRecording(test);
    testAzimuthProgressCompletionAndFrames(test);
    testReverseScanProgressAndCompletion(test);
    testRepeatScanClearsOldBearingResults(test);
    testScanLifecycleEventOrder(test);
    testSpeedChangeRejectedDuringScan(test);
    testDiagnosticsOnFailure(test);
    testAzimuthSampleUpdatesCurrentValue(test);

    return test.result();
}
