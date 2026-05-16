#include "applicationbootstrap.h"

#include "appstate.h"
#include "qmlsingletons.h"

#include <QObject>

namespace siriusscope::app {

ApplicationBootstrap::ApplicationBootstrap()
    : m_diagnosticsSink(std::make_unique<infrastructure::NullDiagnosticsSink>())
    , m_waterfallStorage(std::make_unique<infrastructure::NullWaterfallStorage>())
    , m_bcoSampleSource(std::make_unique<hardware::SimulatorBcoSampleSource>(
          hardware::SimulatorBcoSampleSourceConfig{},
          m_diagnosticsSink.get()))
    , m_antennaState(std::make_unique<hardware::SimulatorAntennaState>())
    , m_antennaAzimuthSource(std::make_unique<hardware::SimulatorAntennaAzimuthSource>(
          m_antennaState.get(),
          hardware::SimulatorAntennaAzimuthSourceConfig{},
          m_diagnosticsSink.get()))
    , m_bcoControl(std::make_unique<hardware::SimulatorBcoControl>(m_bcoSampleSource.get(),
                                                                   m_diagnosticsSink.get()))
    , m_antennaControl(std::make_unique<hardware::SimulatorAntennaControl>(
          m_antennaState.get(),
          m_diagnosticsSink.get()))
    , m_bandConfigController(&m_bandListModel, m_bcoControl.get(), m_diagnosticsSink.get())
{
    m_bcoSampleSource->setBandConfigs(m_bandListModel.bandConfigs());
    m_waterfallController = std::make_unique<WaterfallController>(&m_viewportModel,
                                                                  m_bcoSampleSource.get(),
                                                                  m_bandListModel.bandConfigs(),
                                                                  m_diagnosticsSink.get());

    QObject::connect(&m_bandConfigController,
                     &BandConfigController::bandStateChanged,
                     m_waterfallController.get(),
                     [this](int, double, double, double, bool) {
                         if (m_waterfallController) {
                             m_waterfallController->setBandConfigs(m_bandListModel.bandConfigs());
                         }
                     });

    m_waterfallController->start();
}

void ApplicationBootstrap::registerQmlSingletons()
{
    AppStateQmlSingleton::instance = &AppState::instance();
    FrequencyViewportModelQmlSingleton::instance = &m_viewportModel;
    FrequencyGridModelQmlSingleton::instance = &m_frequencyGridModel;
    SpectrumControllerQmlSingleton::instance = &m_spectrumController;
    SpectrumDecimatorQmlSingleton::instance = &m_spectrumDecimator;
    WaterfallControllerQmlSingleton::instance = m_waterfallController.get();
    AntennaControllerQmlSingleton::instance = &m_antennaController;
    BandListModelQmlSingleton::instance = &m_bandListModel;
    BandConfigControllerQmlSingleton::instance = &m_bandConfigController;
}

} // namespace siriusscope::app
