#include "applicationbootstrap.h"

#include "appstate.h"
#include "qmlsingletons.h"

#include <QObject>

namespace siriusscope::app {

ApplicationBootstrap::ApplicationBootstrap()
    : m_waterfallController(&m_viewportModel)
    , m_diagnosticsSink(std::make_unique<infrastructure::NullDiagnosticsSink>())
    , m_waterfallStorage(std::make_unique<infrastructure::NullWaterfallStorage>())
    , m_bcoControl(std::make_unique<hardware::StubBcoControl>())
    , m_antennaControl(std::make_unique<hardware::StubAntennaControl>())
    , m_bandConfigController(&m_bandListModel, m_bcoControl.get(), m_diagnosticsSink.get())
{
    QObject::connect(&m_bandConfigController,
                     &BandConfigController::bandStateChanged,
                     &m_waterfallController,
                     &WaterfallControllerStub::setSyntheticBand);
}

void ApplicationBootstrap::registerQmlSingletons()
{
    AppStateQmlSingleton::instance = &AppState::instance();
    FrequencyViewportModelQmlSingleton::instance = &m_viewportModel;
    FrequencyGridModelQmlSingleton::instance = &m_frequencyGridModel;
    SpectrumControllerQmlSingleton::instance = &m_spectrumController;
    SpectrumDecimatorQmlSingleton::instance = &m_spectrumDecimator;
    WaterfallControllerQmlSingleton::instance = &m_waterfallController;
    AntennaControllerQmlSingleton::instance = &m_antennaController;
    BandListModelQmlSingleton::instance = &m_bandListModel;
    BandConfigControllerQmlSingleton::instance = &m_bandConfigController;
}

} // namespace siriusscope::app
