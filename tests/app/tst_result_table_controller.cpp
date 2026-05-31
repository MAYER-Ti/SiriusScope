#include "app/resulttablecontroller.h"
#include "app/resulttablemodel.h"
#include "infrastructure/interfaces/result_table_storage.h"

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
                                      int bandIndex = 1,
                                      double bearingAzimuthDeg = 47.0,
                                      std::vector<std::int64_t> frequenciesHz =
                                          {3'000'000'000LL},
                                      std::optional<double> quality = 0.84)
{
    auto created = core::BearingResult::create(sampleIndex,
                                               utcNs,
                                               bandIndex,
                                               bearingAzimuthDeg,
                                               frequenciesHz,
                                               quality);
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
        std::nullopt,
        std::nullopt,
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

const core::ResultTableRow* findRowByBand(const std::vector<core::ResultTableRow>& rows,
                                          int bandIndex)
{
    const auto it = std::find_if(rows.begin(), rows.end(), [bandIndex](const auto& row) {
        return row.bandIndex == bandIndex;
    });
    return it == rows.end() ? nullptr : &(*it);
}

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
    test.require(std::abs(model.rows().front().bearingAzimuthDeg - 47.0) <= 0.001,
                 "controller preserves calculated bearing azimuth");
    test.require(model.data(model.index(0, 0), ResultTableModel::AzimuthTextRole).toString()
                     == QStringLiteral("47,0°"),
                 "controller exposes calculated bearing azimuth to the table");
    test.require(model.rows().front().antennaAzimuthDeg == 50.0,
                 "controller uses append context antenna azimuth");
}

void testAppendMapsSignalParametersByBand(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    app::ResultTableAppendContext context;
    context.scanSessionId = 42;
    context.antennaAzimuthDeg = 50.0;
    context.signalParameters.push_back(processing::SignalParameters{
        1,
        2,
        10'000.0,
        100'000.0,
        {3'000'000'000LL},
    });

    const auto result = controller.appendBearingResults(context, {makeBearingResult()});

    test.require(result.success, "append with signal parameters is accepted");
    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "signal parameter append worker finishes");
    waitUntil([&model] {
        return model.count() == 1;
    });

    test.require(model.rows().front().pulseRepetitionPeriodUs
                     && *model.rows().front().pulseRepetitionPeriodUs == 100'000.0,
                 "controller maps PRI by band index");
    test.require(model.rows().front().pulseWidthUs
                     && *model.rows().front().pulseWidthUs == 10'000.0,
                 "controller maps pulse width by band index");
}

void testAppendAggregatesMultipleBearingResultsByBand(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    app::ResultTableAppendContext context;
    context.scanSessionId = 42;
    context.antennaAzimuthDeg = 50.0;
    context.signalParameters.push_back(processing::SignalParameters{
        0,
        3,
        10'000.0,
        100'000.0,
        {1'000'000'000LL, 1'005'000'000LL, 1'010'000'000LL},
    });

    const auto result = controller.appendBearingResults(
        context,
        {
            makeBearingResult(12,
                              1'700'000'000'000'000'000LL,
                              0,
                              30.0,
                              {1'000'000'000LL},
                              0.4),
            makeBearingResult(13,
                              1'700'000'001'000'000'000LL,
                              0,
                              32.0,
                              {1'005'000'000LL},
                              0.8),
            makeBearingResult(14,
                              1'700'000'002'000'000'000LL,
                              0,
                              31.0,
                              {1'010'000'000LL},
                              0.6),
        });

    test.require(result.success, "aggregated append is accepted");
    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "aggregated append worker finishes");
    waitUntil([&model] {
        return model.count() == 1;
    });

    test.require(model.count() == 1, "same band results produce one result table row");
    if (model.count() != 1) {
        return;
    }
    test.require(storage.appendCalls == 1, "same band aggregation writes one storage row");
    const auto& row = model.rows().front();
    test.require(row.bandIndex == 0, "aggregated row keeps band index");
    test.require(row.frequenciesHz
                     == std::vector<std::int64_t>{
                         1'000'000'000LL,
                         1'005'000'000LL,
                         1'010'000'000LL,
                     },
                 "aggregated row keeps all unique sorted frequencies");
    test.require(row.pulseRepetitionPeriodUs && *row.pulseRepetitionPeriodUs == 100'000.0,
                 "aggregated row maps PRI by band index");
    test.require(row.pulseWidthUs && *row.pulseWidthUs == 10'000.0,
                 "aggregated row maps pulse width by band index");
    test.require(diagnostics.contains("Aggregated 3 bearing results into 1 result table rows"),
                 "aggregation is diagnosed when row count changes");
}

void testAppendAggregatesToOneRowPerBand(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    const auto result = controller.appendBearingResults(
        {42, 50.0},
        {
            makeBearingResult(12,
                              1'700'000'000'000'000'000LL,
                              0,
                              30.0,
                              {1'000'000'000LL},
                              0.4),
            makeBearingResult(13,
                              1'700'000'001'000'000'000LL,
                              0,
                              32.0,
                              {1'005'000'000LL},
                              0.8),
            makeBearingResult(14,
                              1'700'000'002'000'000'000LL,
                              1,
                              120.0,
                              {2'000'000'000LL},
                              0.7),
        });

    test.require(result.success, "two-band append is accepted");
    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "two-band append worker finishes");
    waitUntil([&model] {
        return model.count() == 2;
    });

    test.require(model.count() == 2, "two band indexes produce two rows");
    if (model.count() != 2) {
        return;
    }
    test.require(storage.appendCalls == 2, "two band aggregation writes two storage rows");
    const auto* band0 = findRowByBand(model.rows(), 0);
    const auto* band1 = findRowByBand(model.rows(), 1);
    test.require(band0 != nullptr, "band 0 row exists");
    test.require(band1 != nullptr, "band 1 row exists");
    test.require(band0 && band0->frequenciesHz
                     == std::vector<std::int64_t>{1'000'000'000LL, 1'005'000'000LL},
                 "band 0 row has aggregated frequencies");
    test.require(band1 && band1->frequenciesHz
                     == std::vector<std::int64_t>{2'000'000'000LL},
                 "band 1 row keeps its frequency");
}

void testAppendWithoutMatchingSignalParametersKeepsRow(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    app::ResultTableAppendContext context;
    context.scanSessionId = 42;
    context.antennaAzimuthDeg = 50.0;
    context.signalParameters.push_back(processing::SignalParameters{
        0,
        2,
        10'000.0,
        100'000.0,
        {3'000'000'000LL},
    });

    const auto result = controller.appendBearingResults(context, {makeBearingResult()});

    test.require(result.success, "append without matching signal parameters is accepted");
    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "no-match signal parameter append worker finishes");
    waitUntil([&model] {
        return model.count() == 1;
    });

    test.require(!model.rows().front().pulseRepetitionPeriodUs,
                 "missing band parameters leave PRI empty");
    test.require(!model.rows().front().pulseWidthUs,
                 "missing band parameters leave pulse width empty");
}

void testInvalidSignalParametersDoNotDropBearingRow(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    app::ResultTableAppendContext context;
    context.scanSessionId = 42;
    context.antennaAzimuthDeg = 50.0;
    context.signalParameters.push_back(processing::SignalParameters{
        1,
        2,
        100'000.0,
        10'000.0,
        {3'000'000'000LL},
    });

    const auto result = controller.appendBearingResults(context, {makeBearingResult()});

    test.require(result.success, "invalid signal parameters do not reject append");
    test.require(controller.waitUntilIdle(std::chrono::milliseconds{1500}),
                 "invalid signal parameter append worker finishes");
    waitUntil([&model] {
        return model.count() == 1;
    });

    test.require(model.count() == 1, "bearing row is still appended");
    test.require(!model.rows().front().pulseRepetitionPeriodUs,
                 "invalid signal parameters leave PRI empty");
    test.require(!model.rows().front().pulseWidthUs,
                 "invalid signal parameters leave pulse width empty");
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
    test.require(diagnostics.contains("Skipped invalid aggregated bearing result for band 99"),
                 "invalid aggregate is diagnosed");
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

void testAppendNewerResultAppearsAtTop(TestRunner& test)
{
    ResultTableModel model;
    FakeResultTableStorage storage;
    RecordingDiagnosticsSink diagnostics;
    ResultTableController controller(&model, &storage, &diagnostics);

    const auto older = makeBearingResult(12, 1'700'000'000'000'000'000LL, 1);
    const auto newer = makeBearingResult(13, 1'700'000'001'000'000'000LL, 1);

    controller.appendBearingResults({42, 50.0}, {older});
    controller.waitUntilIdle(std::chrono::milliseconds{1500});
    waitUntil([&model] {
        return model.count() == 1;
    });

    controller.appendBearingResults({42, 50.0}, {newer});
    controller.waitUntilIdle(std::chrono::milliseconds{1500});
    waitUntil([&model] {
        return model.count() == 2;
    });

    test.require(model.count() == 2, "two appends reach model");
    test.require(model.rows().front().sampleIndex == 13,
                 "newer async append is visible at the top of the table");
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
    testAppendMapsSignalParametersByBand(test);
    testAppendAggregatesMultipleBearingResultsByBand(test);
    testAppendAggregatesToOneRowPerBand(test);
    testAppendWithoutMatchingSignalParametersKeepsRow(test);
    testInvalidSignalParametersDoNotDropBearingRow(test);
    testInvalidBearingResultRejected(test);
    testStorageFailureDoesNotUpdateModel(test);
    testReloadLoadsRows(test);
    testAppendNewerResultAppearsAtTop(test);
    testDuplicateAppendSkipped(test);

    return test.result();
}
