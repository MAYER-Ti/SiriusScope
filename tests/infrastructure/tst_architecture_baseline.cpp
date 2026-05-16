#include "infrastructure/interfaces/diagnostics_sink.h"
#include "infrastructure/interfaces/settings_storage.h"
#include "infrastructure/interfaces/waterfall_storage.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace siriusscope::infrastructure;

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

void testDiagnosticEvent(TestRunner& test)
{
    const DiagnosticEvent event{
        DiagnosticSeverity::Warning,
        "hardware",
        "sample stream delayed",
        std::chrono::system_clock::now(),
    };

    test.require(event.severity == DiagnosticSeverity::Warning,
                 "diagnostic event preserves severity");
    test.require(event.subsystem == "hardware", "diagnostic event preserves subsystem");
    test.require(event.message == "sample stream delayed", "diagnostic event preserves message");
}

void testAppSettingsDefaults(TestRunner& test)
{
    const AppSettings settings;

    test.require(settings.connectionMode == ConnectionMode::Simulator,
                 "default connection mode is simulator");
    test.require(settings.dataDirectory.generic_string() == "data",
                 "default data directory is data");
    test.require(settings.waterfallHistoryDepthRows == 360,
                 "default waterfall history depth is 360 rows");
    test.require(settings.maxArchiveFileCount == 16,
                 "default archive file count is 16");
}

void testNullDiagnosticsSink(TestRunner& test)
{
    NullDiagnosticsSink sink;
    sink.publish(DiagnosticEvent{DiagnosticSeverity::Info, "test", "ignored", {}});

    test.require(true, "null diagnostics sink accepts events");
}

void testNullWaterfallStorage(TestRunner& test)
{
    NullWaterfallStorage storage;
    const auto appendResult = storage.append(siriusscope::processing::WaterfallRow{});
    const auto rows = storage.readRange(1, 10, 100);

    test.require(appendResult.success, "null waterfall storage accepts append");
    test.require(rows.empty(), "null waterfall storage returns empty history");
}

} // namespace

int main()
{
    TestRunner test;

    testDiagnosticEvent(test);
    testAppSettingsDefaults(test);
    testNullDiagnosticsSink(test);
    testNullWaterfallStorage(test);

    return test.result();
}
