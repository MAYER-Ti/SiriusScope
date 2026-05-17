#include "app/applicationbootstrap.h"
#include "app/qmlsingletons.h"

#include <QCoreApplication>
#include <QStandardPaths>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

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

void testBootstrapProvidesObjects(TestRunner& test)
{
    siriusscope::app::ApplicationBootstrap bootstrap;

    test.require(bootstrap.frequencyViewportModel() != nullptr,
                 "bootstrap provides frequency viewport model");
    test.require(bootstrap.frequencyGridModel() != nullptr,
                 "bootstrap provides frequency grid model");
    test.require(bootstrap.spectrumController() != nullptr,
                 "bootstrap provides spectrum controller");
    test.require(bootstrap.spectrumDecimator() != nullptr,
                 "bootstrap provides spectrum decimator");
    test.require(bootstrap.waterfallController() != nullptr,
                 "bootstrap provides waterfall controller");
    test.require(bootstrap.antennaController() != nullptr,
                 "bootstrap provides antenna controller");
    test.require(bootstrap.bandListModel() != nullptr,
                 "bootstrap provides band list model");
    test.require(bootstrap.bandConfigController() != nullptr,
                 "bootstrap provides band config controller");
    test.require(bootstrap.diagnosticsSink() != nullptr,
                 "bootstrap provides diagnostics sink");
    test.require(bootstrap.diagnosticsService() != nullptr,
                 "bootstrap provides diagnostics service");
    test.require(bootstrap.statusModel() != nullptr,
                 "bootstrap provides status model");
    test.require(bootstrap.scanController() != nullptr,
                 "bootstrap provides scan controller");
    test.require(bootstrap.bearingFrameBus() != nullptr,
                 "bootstrap provides bearing frame bus");
    test.require(bootstrap.scanAcquisitionRecorder() != nullptr,
                 "bootstrap provides scan acquisition recorder");
    test.require(bootstrap.processingFlushControl() != nullptr,
                 "bootstrap provides processing flush control");
    test.require(bootstrap.scanRecordingControl() != nullptr,
                 "bootstrap provides scan recording control");
    test.require(bootstrap.resultTableSink() != nullptr,
                 "bootstrap provides result table sink");
    test.require(bootstrap.waterfallStorage() != nullptr,
                 "bootstrap provides waterfall storage placeholder");
    test.require(bootstrap.bcoControl() != nullptr,
                 "bootstrap provides BCO control");
    test.require(bootstrap.bcoSampleSource() != nullptr,
                 "bootstrap provides BCO sample source");
    test.require(bootstrap.antennaControl() != nullptr,
                 "bootstrap provides antenna control");
    test.require(bootstrap.antennaAzimuthSource() != nullptr,
                 "bootstrap provides antenna azimuth source");

    bootstrap.registerQmlSingletons();

    test.require(siriusscope::app::FrequencyViewportModelQmlSingleton::instance
                     == bootstrap.frequencyViewportModel(),
                 "bootstrap registers frequency viewport singleton");
    test.require(siriusscope::app::WaterfallControllerQmlSingleton::instance
                     == bootstrap.waterfallController(),
                 "bootstrap registers waterfall controller singleton");
    test.require(siriusscope::app::AntennaControllerQmlSingleton::instance
                     == bootstrap.antennaController(),
                 "bootstrap registers antenna controller singleton");
    test.require(siriusscope::app::ScanControllerQmlSingleton::instance
                     == bootstrap.scanController(),
                 "bootstrap registers scan controller singleton");
    test.require(siriusscope::app::BandListModelQmlSingleton::instance
                     == bootstrap.bandListModel(),
                 "bootstrap registers band list model singleton");
    test.require(siriusscope::app::BandConfigControllerQmlSingleton::instance
                     == bootstrap.bandConfigController(),
                 "bootstrap registers band config controller singleton");
    test.require(siriusscope::app::DiagnosticsServiceQmlSingleton::instance
                     == bootstrap.diagnosticsService(),
                 "bootstrap registers diagnostics service singleton");
    test.require(siriusscope::app::StatusModelQmlSingleton::instance
                     == bootstrap.statusModel(),
                 "bootstrap registers status model singleton");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    TestRunner test;

    testBootstrapProvidesObjects(test);

    return test.result();
}
