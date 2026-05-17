#include "app/resulttablecontroller.h"
#include "app/resulttablemodel.h"
#include "infrastructure/interfaces/result_table_storage.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

using namespace siriusscope;
using siriusscope::app::ResultTableController;
using siriusscope::app::ResultTableModel;

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

core::BearingResult makeBearingResult(std::uint64_t sampleIndex = 12,
                                      std::int64_t utcNs =
                                          1'700'000'000'000'000'000LL,
                                      int bandIndex = 1)
{
    auto created = core::BearingResult::create(sampleIndex,
                                               utcNs,
                                               bandIndex,
                                               47.0,
                                               {3'000'000'000LL},
                                               0.84);
    return *created.value();
}

core::ResultTableRow makeRow(std::uint64_t sampleIndex = 12,
                             std::int64_t utcNs = 1'700'000'000'000'000'000LL,
                             int bandIndex = 1)
{
    return core::ResultTableRow{
        sampleIndex,
        utcNs,
        47.0,
        51.0,
        bandIndex,
        {3'000'000'000LL},
        0.84,
        {},
    };
}

class FakeResultTableStorage final : public infrastructure::IResultTableStorage
{
public:
    core::OperationResult append(const core::ResultTableRow& row) override
    {
        std::lock_guard lock(mutex);
        ++appendCalls;
        if (failAppend) {
            return core::OperationResult::failure("append failed");
        }
        appendedRows.push_back(row);
        return core::OperationResult::ok();
    }

    std::vector<core::ResultTableRow> readAll() override
    {
        std::lock_guard lock(mutex);
        ++readCalls;
        return readRows;
    }

    mutable std::mutex mutex;
    bool failAppend = false;
    int appendCalls = 0;
    int readCalls = 0;
    std::vector<core::ResultTableRow> appendedRows;
    std::vector<core::ResultTableRow> readRows;
};

class RecordingDiagnosticsSink final : public infrastructure::IDiagnosticsSink
{
public:
    void publish(const infrastructure::DiagnosticEvent& event) override
    {
        std::lock_guard lock(mutex);
        events.push_back(event);
    }

    bool contains(const std::string& text) const
    {
        std::lock_guard lock(mutex);
        for (const auto& event : events) {
            if (event.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex mutex;
    std::vector<infrastructure::DiagnosticEvent> events;
};

void testEmptyAppend(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    const auto result = controller.appendBearingResults({42, 50.0}, {});

    test.require(result.success, "empty append succeeds");
    test.require(model.count() == 0, "empty append does not change model");
    test.require(storage.appendCalls == 0, "empty append does not call storage");
    test.require(diagnostics.contains("No bearing results"), "empty append is diagnosed");
}

void testAppendValidBearingResult(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    const auto result = controller.appendBearingResults({42, 50.0}, {makeBearingResult()});

    test.require(result.success, "valid append is accepted");
    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "valid append worker finishes");
    const bool delivered = waitUntil([&model] {
        return model.count() == 1;
    });

    test.require(delivered, "valid append reaches model");
    test.require(storage.appendCalls == 1, "valid append writes storage");
    test.require(model.rows().front().bearingAzimuthDeg == 47.0,
                 "controller preserves calculated bearing azimuth");
    test.require(model.data(model.index(0, 0), ResultTableModel::AzimuthTextRole).toString()
                     == QStringLiteral("47,0°"),
                 "controller exposes calculated bearing azimuth to the table");
    test.require(model.rows().front().antennaAzimuthDeg == 50.0,
                 "controller uses append context antenna azimuth");
}

void testInvalidBearingResultRejected(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    core::BearingResult invalid;
    invalid.sampleIndex = 12;
    invalid.resultTimeUtcNs = -1;
    invalid.bandIndex = 99;
    invalid.bearingAzimuthDeg = 47.0;
    invalid.frequenciesHz = {3'000'000'000LL};
    invalid.quality = 0.84;

    const auto result = controller.appendBearingResults({42, 50.0}, {invalid});

    test.require(!result.success, "invalid append reports failure");
    test.require(model.count() == 0, "invalid append does not reach model");
    test.require(storage.appendCalls == 0, "invalid append does not call storage");
    test.require(diagnostics.contains("Rejected invalid result table row"),
                 "invalid append is diagnosed");
}

void testStorageFailureDoesNotUpdateModel(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    storage.failAppend = true;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    const auto result = controller.appendBearingResults({42, 50.0}, {makeBearingResult()});

    test.require(result.success, "storage failure append is queued");
    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "storage failure worker finishes");
    const bool delivered = waitUntil([&controller] {
        return controller.lastError().contains(QStringLiteral("append failed"));
    });

    test.require(delivered, "storage failure updates lastError");
    test.require(model.count() == 0, "storage failure does not update model");
    test.require(storage.appendCalls == 1, "storage failure attempted write");
    test.require(diagnostics.contains("Result table append failed"),
                 "storage failure is diagnosed");
}

void testReloadLoadsRows(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    storage.readRows = {makeRow()};
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    controller.reload();

    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "reload worker finishes");
    const bool delivered = waitUntil([&controller, &model] {
        return controller.loaded() && model.count() == 1;
    });

    test.require(delivered, "reload populates model");
    test.require(storage.readCalls == 1, "reload reads storage once");
}

void testDuplicateAppendSkipped(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    const auto bearing = makeBearingResult();
    controller.appendBearingResults({42, 50.0}, {bearing});
    controller.waitUntilIdle(std::chrono::milliseconds{1500});
    waitUntil([&model] {
        return model.count() == 1;
    });

    const auto second = controller.appendBearingResults({42, 50.0}, {bearing});
    controller.waitUntilIdle(std::chrono::milliseconds{1500});
    waitUntil([&model] {
        return model.count() == 1;
    });

    test.require(second.success, "duplicate append succeeds as no-op");
    test.require(model.count() == 1, "duplicate append does not change model");
    test.require(storage.appendCalls == 1, "duplicate append does not write storage");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testEmptyAppend(test);
    testAppendValidBearingResult(test);
    testInvalidBearingResultRejected(test);
    testStorageFailureDoesNotUpdateModel(test);
    testReloadLoadsRows(test);
    testDuplicateAppendSkipped(test);

    return test.result();
}
