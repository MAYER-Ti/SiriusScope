#include "app/appstate.h"
#include "app/diagnosticsservice.h"
#include "app/frequencyviewportmodel.h"
#include "app/scancontroller.h"
#include "app/statusmodel.h"
#include "app/waterfallcontroller.h"
#include "app/waterfallstorage.h"
#include "core/domain_models.h"

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

class FakeSampleSource final : public hardware::IBcoSampleSource
{
public:
    core::OperationResult start(SampleBatchCallback callback) override
    {
        m_callback = std::move(callback);
        m_running = true;
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        m_running = false;
        return core::OperationResult::ok();
    }

private:
    SampleBatchCallback m_callback;
    bool m_running = false;
};

class FakeAzimuthSource final : public hardware::IAntennaAzimuthSource
{
public:
    core::OperationResult start(AzimuthCallback callback) override
    {
        m_callback = std::move(callback);
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
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

private:
    AzimuthCallback m_callback;
};

std::vector<core::BandConfig> makeBandConfigs()
{
    std::vector<core::BandConfig> bands;
    for (int bandIndex = 0; bandIndex < core::DomainConstraints::currentBandCount; ++bandIndex) {
        const auto created = core::BandConfig::create(
            bandIndex,
            550'000'000LL + static_cast<std::int64_t>(bandIndex)
                * core::DomainConstraints::maxBandWidthHz,
            core::DomainConstraints::maxBandWidthHz);
        bands.push_back(*created.value());
    }
    return bands;
}

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

void publish(app::DiagnosticsService& diagnostics,
             infrastructure::DiagnosticSeverity severity,
             const std::string& subsystem,
             const std::string& message)
{
    diagnostics.publish(infrastructure::DiagnosticEvent{
        severity,
        subsystem,
        message,
        std::chrono::system_clock::now(),
    });
}

void testInitialStatuses(TestRunner& test)
{
    AppState::instance().setModeChangeLocked(false);
    AppState::instance().setMode(AppState::Mode::Test);

    FrequencyViewportModel viewport;
    FakeSampleSource source;
    InMemoryWaterfallSessionStorage storage;
    app::DiagnosticsService diagnostics;
    FakeAzimuthSource azimuthSource;
    app::ScanController scanController(nullptr, &azimuthSource, nullptr, &diagnostics);
    app::WaterfallControllerConfig config;
    config.sourceFlushIntervalMs = 20;
    app::WaterfallController controller(&viewport,
                                        &source,
                                        makeBandConfigs(),
                                        &storage,
                                        &diagnostics,
                                        config);
    app::StatusModel model(&diagnostics, &AppState::instance(), &controller, &scanController);

    test.require(model.programValue() == QStringLiteral("работает"),
                 "initial program status is running");
    test.require(model.programLevel() == app::StatusModel::StatusLevel::Good,
                 "initial program level is good");
    test.require(model.modeValue() == QStringLiteral("генератор"),
                 "initial mode is generator");
    test.require(model.bcoValue() == QStringLiteral("ожидание потока"),
                 "initial BCO status waits for stream");
    test.require(model.recordingValue() == QStringLiteral("выключена"),
                 "initial recording status is disabled");
    test.require(model.diagnosticsValue() == QStringLiteral("ошибок нет"),
                 "initial diagnostics status reports no errors");
    test.require(model.azimuthValue() == QStringLiteral("000,0°"),
                 "initial azimuth is formatted with leading zeroes");
}

void testModeAndSourceStatuses(TestRunner& test)
{
    AppState::instance().setModeChangeLocked(false);
    AppState::instance().setMode(AppState::Mode::Test);

    FrequencyViewportModel viewport;
    FakeSampleSource source;
    InMemoryWaterfallSessionStorage storage;
    app::DiagnosticsService diagnostics;
    FakeAzimuthSource azimuthSource;
    app::ScanController scanController(nullptr, &azimuthSource, nullptr, &diagnostics);
    app::WaterfallControllerConfig config;
    config.sourceFlushIntervalMs = 20;
    app::WaterfallController controller(&viewport,
                                        &source,
                                        makeBandConfigs(),
                                        &storage,
                                        &diagnostics,
                                        config);
    app::StatusModel model(&diagnostics, &AppState::instance(), &controller, &scanController);

    AppState::instance().setMode(AppState::Mode::Combat);
    controller.start();

    test.require(model.modeValue() == QStringLiteral("аппаратура"),
                 "mode status follows AppState");
    test.require(model.bcoValue() == QStringLiteral("поток активен"),
                 "BCO status follows sourceActive");
    test.require(model.bcoLevel() == app::StatusModel::StatusLevel::Good,
                 "active BCO stream is good");
}

void testDiagnosticRules(TestRunner& test)
{
    AppState::instance().setModeChangeLocked(false);
    AppState::instance().setMode(AppState::Mode::Test);

    FrequencyViewportModel viewport;
    FakeSampleSource source;
    InMemoryWaterfallSessionStorage storage;
    app::DiagnosticsService diagnostics;
    FakeAzimuthSource azimuthSource;
    app::ScanController scanController(nullptr, &azimuthSource, nullptr, &diagnostics);
    app::WaterfallControllerConfig config;
    config.sourceFlushIntervalMs = 20;
    app::WaterfallController controller(&viewport,
                                        &source,
                                        makeBandConfigs(),
                                        &storage,
                                        &diagnostics,
                                        config);
    app::StatusModel model(&diagnostics, &AppState::instance(), &controller, &scanController);

    publish(diagnostics,
            infrastructure::DiagnosticSeverity::Info,
            "SimulatorBcoControl",
            "BCO simulator applied BandConfig for band 1");
    waitUntil([&model] { return model.bcoControlValue() == QStringLiteral("применено"); });
    test.require(model.bcoControlLevel() == app::StatusModel::StatusLevel::Good,
                 "BCO control applied event is good");

    publish(diagnostics,
            infrastructure::DiagnosticSeverity::Warning,
            "BandConfig",
            "Band 2 settings rejected: invalid width");
    waitUntil([&model] { return model.bcoControlValue() == QStringLiteral("отклонено"); });
    test.require(model.bcoControlLevel() == app::StatusModel::StatusLevel::Warning,
                 "BandConfig rejection updates BCO control warning");

    publish(diagnostics,
            infrastructure::DiagnosticSeverity::Warning,
            "SimulatorAntennaControl",
            "scan sector crosses antenna blind zone 170..190 degrees");
    waitUntil([&model] { return model.antennaValue() == QStringLiteral("слепая зона"); });
    test.require(model.antennaLevel() == app::StatusModel::StatusLevel::Warning,
                 "antenna blind zone warning updates antenna status");

    publish(diagnostics,
            infrastructure::DiagnosticSeverity::Warning,
            "WaterfallProcessing",
            "dropped 2 queued BCO sample batches containing 20 samples");
    waitUntil([&model] { return model.bcoValue() == QStringLiteral("потери очереди"); });
    test.require(model.bcoLevel() == app::StatusModel::StatusLevel::Warning,
                 "dropped batches update BCO queue pressure status");

    publish(diagnostics,
            infrastructure::DiagnosticSeverity::Error,
            "WaterfallStorage",
            "Cannot open waterfall.bin");
    waitUntil([&model] { return model.programValue() == QStringLiteral("ошибка"); });
    test.require(model.programLevel() == app::StatusModel::StatusLevel::Error,
                 "storage error escalates program status");
    test.require(model.recordingValue() == QStringLiteral("ошибка записи"),
                 "storage error updates recording status");
    test.require(model.diagnosticsValue() == QStringLiteral("Cannot open waterfall.bin"),
                 "diagnostics chip shows latest important message");
}

void testRecordingAndAzimuthStatuses(TestRunner& test)
{
    AppState::instance().setModeChangeLocked(false);
    AppState::instance().setMode(AppState::Mode::Test);

    FrequencyViewportModel viewport;
    FakeSampleSource source;
    InMemoryWaterfallSessionStorage storage;
    app::DiagnosticsService diagnostics;
    FakeAzimuthSource azimuthSource;
    app::ScanController scanController(nullptr, &azimuthSource, nullptr, &diagnostics);
    app::WaterfallControllerConfig config;
    config.sourceFlushIntervalMs = 20;
    app::WaterfallController controller(&viewport,
                                        &source,
                                        makeBandConfigs(),
                                        &storage,
                                        &diagnostics,
                                        config);
    app::StatusModel model(&diagnostics, &AppState::instance(), &controller, &scanController);

    controller.startRecording();
    test.require(model.recordingValue() == QStringLiteral("включена"),
                 "startRecording updates recording chip");
    test.require(model.recordingLevel() == app::StatusModel::StatusLevel::Good,
                 "active recording is good");

    azimuthSource.emitAzimuth(5.2);
    waitUntil([&model] { return model.azimuthValue() == QStringLiteral("005,2°"); });
    test.require(model.azimuthValue() == QStringLiteral("005,2°"),
                 "azimuth changes are formatted by StatusModel");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testInitialStatuses(test);
    testModeAndSourceStatuses(test);
    testDiagnosticRules(test);
    testRecordingAndAzimuthStatuses(test);

    return test.result();
}
