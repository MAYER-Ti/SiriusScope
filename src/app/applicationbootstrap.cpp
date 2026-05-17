#include "applicationbootstrap.h"

#include "appstate.h"
#include "infrastructure/storage/binary_waterfall_session_storage.h"
#include "qmlsingletons.h"

#include <QDir>
#include <QObject>
#include <QStandardPaths>

#include <chrono>

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
    : m_diagnosticLogWriter(std::make_unique<infrastructure::DiagnosticLogWriter>(
          infrastructure::DiagnosticLogWriter::Config{
              defaultWaterfallDataRootPath(),
          }))
    , m_diagnosticsService(std::make_unique<DiagnosticsService>(m_diagnosticLogWriter.get()))
    , m_waterfallStorage(std::make_unique<infrastructure::NullWaterfallStorage>())
    , m_waterfallSessionStorage(
          std::make_unique<infrastructure::BinaryWaterfallSessionStorage>(
              infrastructure::BinaryWaterfallSessionStorage::Config{
                  defaultWaterfallDataRootPath(),
                  20,
                  false,
              },
              m_diagnosticsService.get()))
    , m_bcoSampleSource(std::make_unique<hardware::SimulatorBcoSampleSource>(
          hardware::SimulatorBcoSampleSourceConfig{},
          m_diagnosticsService.get()))
    , m_antennaState(std::make_unique<hardware::SimulatorAntennaState>())
    , m_antennaAzimuthSource(std::make_unique<hardware::SimulatorAntennaAzimuthSource>(
          m_antennaState.get(),
          hardware::SimulatorAntennaAzimuthSourceConfig{},
          m_diagnosticsService.get()))
    , m_bcoControl(std::make_unique<hardware::SimulatorBcoControl>(m_bcoSampleSource.get(),
                                                                   m_diagnosticsService.get()))
    , m_antennaControl(std::make_unique<hardware::SimulatorAntennaControl>(
          m_antennaState.get(),
          m_diagnosticsService.get()))
    , m_bearingFrameBus(std::make_unique<BearingFrameBus>())
    , m_bandConfigController(&m_bandListModel, m_bcoControl.get(), m_diagnosticsService.get())
{
    m_bcoSampleSource->setBandConfigs(m_bandListModel.bandConfigs());
    m_waterfallController = std::make_unique<WaterfallController>(&m_viewportModel,
                                                                  m_bcoSampleSource.get(),
                                                                  m_bandListModel.bandConfigs(),
                                                                  m_waterfallSessionStorage.get(),
                                                                  m_diagnosticsService.get(),
                                                                  WaterfallControllerConfig{},
                                                                  m_bearingFrameBus.get());

    m_scanController = std::make_unique<ScanController>(m_antennaControl.get(),
                                                        m_antennaAzimuthSource.get(),
                                                        m_bearingFrameBus.get(),
                                                        m_diagnosticsService.get());

    m_statusModel = std::make_unique<StatusModel>(m_diagnosticsService.get(),
                                                  &AppState::instance(),
                                                  m_waterfallController.get(),
                                                  m_scanController.get());

    QObject::connect(&m_antennaController,
                     &AntennaControllerStub::commandRejected,
                     m_diagnosticsService.get(),
                     [this](const QString& reason) {
                         if (!m_diagnosticsService) {
                             return;
                         }

                         m_diagnosticsService->publish(infrastructure::DiagnosticEvent{
                             infrastructure::DiagnosticSeverity::Warning,
                             "AntennaController",
                             reason.toStdString(),
                             std::chrono::system_clock::now(),
                         });
                     });

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

    m_diagnosticsService->publish(infrastructure::DiagnosticEvent{
        infrastructure::DiagnosticSeverity::Info,
        "Application",
        "SiriusScope application bootstrap completed",
        std::chrono::system_clock::now(),
    });
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
    ScanControllerQmlSingleton::instance = m_scanController.get();
    BandListModelQmlSingleton::instance = &m_bandListModel;
    BandConfigControllerQmlSingleton::instance = &m_bandConfigController;
    DiagnosticsServiceQmlSingleton::instance = m_diagnosticsService.get();
    StatusModelQmlSingleton::instance = m_statusModel.get();
}

} // namespace siriusscope::app
