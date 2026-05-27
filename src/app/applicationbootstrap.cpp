#include "applicationbootstrap.h"

#include "appstate.h"
#include "infrastructure/storage/binary_result_table_storage.h"
#include "infrastructure/storage/binary_waterfall_session_storage.h"
#include "qmlsingletons.h"

#include <QDir>
#include <QMetaObject>
#include <QObject>
#include <QStandardPaths>

#include <chrono>
#include <cstddef>
#include <vector>

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

std::vector<hardware::SimulatorPulseBandConfig> simulatorPulseConfigsFromBands(
    const BandListModel& model)
{
    std::vector<hardware::SimulatorPulseBandConfig> configs;
    configs.reserve(static_cast<std::size_t>(model.count()));

    for (int row = 0; row < model.count(); ++row) {
        const auto* band = model.bandAt(row);
        if (!band) {
            continue;
        }

        configs.push_back(hardware::SimulatorPulseBandConfig{
            band->config.bandIndex,
            band->config.enabled,
            band->generatorPulsePeriodUs,
            band->generatorPulseWidthUs,
        });
    }

    return configs;
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
    , m_antennaState(std::make_unique<hardware::SimulatorAntennaState>())
    , m_bcoSampleSource(std::make_unique<hardware::SimulatorBcoSampleSource>(
          hardware::SimulatorBcoSampleSourceConfig{},
          m_antennaState.get(),
          m_diagnosticsService.get()))
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
    , m_signalSampleBus(std::make_unique<SignalSampleBus>())
    , m_bearingService(std::make_unique<processing::BearingService>())
    , m_scanAcquisitionRecorder(std::make_unique<InMemoryScanAcquisitionRecorder>())
    , m_resultTableStorage(std::make_unique<infrastructure::BinaryResultTableStorage>(
          infrastructure::BinaryResultTableStorage::Config{
              defaultWaterfallDataRootPath(),
              false,
          },
          m_diagnosticsService.get()))
    , m_resultTableModel(std::make_unique<ResultTableModel>())
    , m_resultTableController(std::make_unique<ResultTableController>(
          m_resultTableModel.get(),
          m_resultTableStorage.get(),
          m_diagnosticsService.get()))
    , m_bandConfigController(&m_bandListModel, m_bcoControl.get(), m_diagnosticsService.get())
{
    m_spectrumEnvelopeController.setDiagnosticsSink(m_diagnosticsService.get());
    m_spectrumEnvelopeWorker =
        new SpectrumEnvelopeWorker(SpectrumEnvelopeWorkerConfig{}, m_diagnosticsService.get());
    m_spectrumEnvelopeWorker->moveToThread(&m_spectrumEnvelopeThread);
    QObject::connect(&m_spectrumEnvelopeThread,
                     &QThread::finished,
                     m_spectrumEnvelopeWorker,
                     &QObject::deleteLater);
    QObject::connect(m_spectrumEnvelopeWorker,
                     &SpectrumEnvelopeWorker::envelopeSnapshotReady,
                     &m_spectrumEnvelopeController,
                     &SpectrumEnvelopeController::acceptSnapshot,
                     Qt::QueuedConnection);
    m_spectrumEnvelopeThread.start();

    m_bcoSampleSource->setBandConfigs(m_bandListModel.bandConfigs());
    m_bcoSampleSource->setPulseBandConfigs(simulatorPulseConfigsFromBands(m_bandListModel));
    createBcoStreamSource();
    configureBcoStreamSource();

    QMetaObject::invokeMethod(m_spectrumEnvelopeWorker,
                              [worker = m_spectrumEnvelopeWorker,
                               minHz = m_viewportModel.viewMinHz(),
                               maxHz = m_viewportModel.viewMaxHz()] {
                                  worker->setViewport(minHz, maxHz);
                              },
                              Qt::QueuedConnection);
    m_waterfallController = std::make_unique<WaterfallController>(&m_viewportModel,
                                                                  m_bcoStreamSource.get(),
                                                                  m_bandListModel.bandConfigs(),
                                                                  m_waterfallSessionStorage.get(),
                                                                  m_diagnosticsService.get(),
                                                                  WaterfallControllerConfig{},
                                                                  m_bearingFrameBus.get(),
                                                                  m_signalSampleBus.get(),
                                                                  m_spectrumEnvelopeWorker);
    m_recordingController = std::make_unique<RecordingController>(m_bcoControl.get(),
                                                                  &m_bandListModel,
                                                                  &m_bandConfigController,
                                                                  m_waterfallController.get(),
                                                                  &m_spectrumEnvelopeController,
                                                                  m_spectrumEnvelopeWorker,
                                                                  m_diagnosticsService.get());
    m_scanRecordingControl =
        std::make_unique<WaterfallScanRecordingAdapter>(m_recordingController.get());

    m_scanController = std::make_unique<ScanController>(m_antennaControl.get(),
                                                        m_antennaAzimuthSource.get(),
                                                        m_bearingFrameBus.get(),
                                                        m_signalSampleBus.get(),
                                                        m_bearingService.get(),
                                                        m_scanAcquisitionRecorder.get(),
                                                        m_waterfallController.get(),
                                                        m_scanRecordingControl.get(),
                                                        m_resultTableController.get(),
                                                        m_diagnosticsService.get());

    m_statusModel = std::make_unique<StatusModel>(m_diagnosticsService.get(),
                                                  &AppState::instance(),
                                                  m_waterfallController.get(),
                                                  m_recordingController.get(),
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
                         configureBcoStreamSource();
                     });
    QObject::connect(&m_bandConfigController,
                     &BandConfigController::generatorPulseSettingsApplied,
                     &m_bandConfigController,
                     [this](int) {
                         const auto pulseConfigs =
                             simulatorPulseConfigsFromBands(m_bandListModel);
                         if (m_bcoSampleSource) {
                             m_bcoSampleSource->setPulseBandConfigs(pulseConfigs);
                         }
                         m_hardwareProfile.simulatorLoadConfig.pulseBandConfigs = pulseConfigs;
                     });
    QObject::connect(&m_viewportModel,
                     &FrequencyViewportModel::viewportChanged,
                     m_spectrumEnvelopeWorker,
                     [worker = m_spectrumEnvelopeWorker](double minHz, double maxHz, const QString&) {
                         worker->setViewport(minHz, maxHz);
                     });

    m_waterfallController->start();
    m_resultTableController->reload();

    m_diagnosticsService->publish(infrastructure::DiagnosticEvent{
        infrastructure::DiagnosticSeverity::Info,
        "Application",
        "SiriusScope application bootstrap completed",
        std::chrono::system_clock::now(),
    });
}

ApplicationBootstrap::~ApplicationBootstrap()
{
    if (m_waterfallController) {
        m_waterfallController->stop();
    }

    if (m_spectrumEnvelopeThread.isRunning()) {
        m_spectrumEnvelopeThread.quit();
        m_spectrumEnvelopeThread.wait();
    }

    m_spectrumEnvelopeWorker = nullptr;
}

hardware::BcoStreamConfig ApplicationBootstrap::makeBcoStreamConfig() const
{
    hardware::BcoStreamConfig config;
    config.bandConfigs = m_bandListModel.bandConfigs();
    config.timeBase = core::TimeBase{
        0,
        0,
        core::DomainConstraints::defaultSamplePeriodNs,
    };
    config.sessionId = 0;
    return config;
}

hardware::HardwareProfile ApplicationBootstrap::makeDefaultHardwareProfile() const
{
    hardware::HardwareProfile profile;
    profile.dataSourceMode = hardware::DataSourceMode::Simulator;
    profile.bcoStreamConfig = makeBcoStreamConfig();
    profile.simulatorLoadConfig.profile = hardware::SimulatorLoadProfile::UiDemo;
    profile.simulatorLoadConfig.samplesPerSecond = 1'280;
    profile.simulatorLoadConfig.batchPeriod = std::chrono::milliseconds{100};
    profile.simulatorLoadConfig.pulseBandConfigs =
        simulatorPulseConfigsFromBands(m_bandListModel);
    return profile;
}

void ApplicationBootstrap::createBcoStreamSource()
{
    m_hardwareProfile = makeDefaultHardwareProfile();

    m_bcoStreamSource =
        hardware::DataSourceFactory::createBcoStreamSourceFromLegacySimulator(
            m_hardwareProfile,
            m_bcoSampleSource.get());

    if (!m_bcoStreamSource && m_diagnosticsService) {
        m_diagnosticsService->publish(infrastructure::DiagnosticEvent{
            infrastructure::DiagnosticSeverity::Error,
            "Application",
            "BCO stream source creation failed",
            std::chrono::system_clock::now(),
        });
    }
}

void ApplicationBootstrap::configureBcoStreamSource()
{
    if (!m_bcoStreamSource) {
        return;
    }

    m_hardwareProfile.bcoStreamConfig = makeBcoStreamConfig();
    m_hardwareProfile.simulatorLoadConfig.pulseBandConfigs =
        simulatorPulseConfigsFromBands(m_bandListModel);

    const auto configured =
        m_bcoStreamSource->configure(m_hardwareProfile.bcoStreamConfig);
    if (!configured && m_diagnosticsService) {
        m_diagnosticsService->publish(infrastructure::DiagnosticEvent{
            infrastructure::DiagnosticSeverity::Error,
            "Application",
            "BCO stream source configure failed: " + configured.message,
            std::chrono::system_clock::now(),
        });
    }
}

void ApplicationBootstrap::registerQmlSingletons()
{
    AppStateQmlSingleton::instance = &AppState::instance();
    FrequencyViewportModelQmlSingleton::instance = &m_viewportModel;
    FrequencyGridModelQmlSingleton::instance = &m_frequencyGridModel;
    SpectrumControllerQmlSingleton::instance = &m_spectrumController;
    SpectrumDecimatorQmlSingleton::instance = &m_spectrumDecimator;
    SpectrumEnvelopeControllerQmlSingleton::instance = &m_spectrumEnvelopeController;
    WaterfallControllerQmlSingleton::instance = m_waterfallController.get();
    RecordingControllerQmlSingleton::instance = m_recordingController.get();
    AntennaControllerQmlSingleton::instance = &m_antennaController;
    ScanControllerQmlSingleton::instance = m_scanController.get();
    BandListModelQmlSingleton::instance = &m_bandListModel;
    BandConfigControllerQmlSingleton::instance = &m_bandConfigController;
    DiagnosticsServiceQmlSingleton::instance = m_diagnosticsService.get();
    StatusModelQmlSingleton::instance = m_statusModel.get();
    ResultTableModelQmlSingleton::instance = m_resultTableModel.get();
}

} // namespace siriusscope::app
