#include "applicationbootstrap.h"

#include "appstate.h"
#include "infrastructure/storage/binary_waterfall_session_storage.h"
#include "qmlsingletons.h"

#include <QDir>
#include <QObject>
#include <QStandardPaths>

namespace siriusscope::app {
namespace {

QString defaultWaterfallDataRootPath()
{
    const QString appDataPath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataPath.isEmpty()) {
        return QDir(appDataPath).filePath(QStringLiteral("SiriusScopeData"));
    }

    return QDir(QDir::currentPath()).filePath(QStringLiteral("SiriusScopeData"));
}

} // namespace

ApplicationBootstrap::ApplicationBootstrap()
    : m_diagnosticsSink(std::make_unique<infrastructure::NullDiagnosticsSink>())
    , m_waterfallStorage(std::make_unique<infrastructure::NullWaterfallStorage>())
    , m_waterfallSessionStorage(
          std::make_unique<infrastructure::BinaryWaterfallSessionStorage>(
              infrastructure::BinaryWaterfallSessionStorage::Config{
                  defaultWaterfallDataRootPath(),
                  20,
                  false,
              },
              m_diagnosticsSink.get()))
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
                                                                  m_waterfallSessionStorage.get(),
                                                                  m_diagnosticsSink.get());

    QObject::connect(&m_bandConfigController,
                     &BandConfigController::bandSettingsApplied,
                     m_waterfallController.get(),
                     [this](int) {
                         if (m_waterfallController) {
                             m_waterfallController->setBandConfigs(m_bandListModel.bandConfigs());
                         }
                     });

    QObject::connect(m_waterfallController.get(),
                     &WaterfallController::recordingStateChanged,
                     &m_bandConfigController,
                     [this]() {
                         m_bandConfigController.setEditingLocked(
                             m_waterfallController && m_waterfallController->sessionActive());
                     });
    QObject::connect(m_waterfallController.get(),
                     &WaterfallController::recordingStateChanged,
                     &AppState::instance(),
                     [this]() {
                         AppState::instance().setModeChangeLocked(
                             m_waterfallController && m_waterfallController->sessionActive());
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
