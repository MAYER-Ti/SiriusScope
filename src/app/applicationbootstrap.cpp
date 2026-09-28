#include "applicationbootstrap.h"
#include "hardware/udp/udp_bco_control.h"

#include "appstate.h"
#include "hardware/simulator/high_load_simulator_bco_control.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/simulator/simulated_bco_payload_accounting.h"
#include "infrastructure/storage/binary_result_table_storage.h"
#include "infrastructure/storage/binary_waterfall_session_storage.h"
#include "qmlsingletons.h"

#include <QDir>
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

pipeline::DataIngestPipelineConfig makeBaselineDataIngestPipelineConfig()
{
    const auto target = hardware::baselineRawThroughput60MbpsTarget();
    const auto samplesPerBatch = hardware::samplesPerBatchForTarget(target);

    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{256, samplesPerBatch};
    config.queueCapacity = 128;
    config.diagnosticsPublishInterval = std::chrono::milliseconds{1000};
    config.acceptingOnStart = false;
    config.processing.processingMode = pipeline::ProcessingMode::ParallelFanOut;
    config.processing.stageQueueCapacity = 128;
    config.processing.enableSignalParameterStage = false;
    return config;
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

ApplicationBootstrap::ApplicationBootstrap(std::optional<hardware::UdpBcoSourceConfig> udpSource)
    : m_udpSourceConfig(std::move(udpSource))
    , m_diagnosticLogWriter(std::make_unique<infrastructure::DiagnosticLogWriter>(
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
    , m_antennaAzimuthSource(std::make_unique<hardware::SimulatorAntennaAzimuthSource>(
          m_antennaState.get(),
          hardware::SimulatorAntennaAzimuthSourceConfig{},
          m_diagnosticsService.get()))
    , m_antennaControl(std::make_unique<hardware::SimulatorAntennaControl>(
          m_antennaState.get(),
          m_diagnosticsService.get()))
    , m_bearingFrameBus(std::make_unique<BearingFrameBus>())
    , m_signalSampleBus(std::make_unique<SignalSampleBus>())
    , m_dataIngestPipeline(std::make_unique<pipeline::DataIngestPipeline>(
          makeBaselineDataIngestPipelineConfig(),
          m_diagnosticsService.get()))
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
{
    m_spectrumEnvelopeController.setDiagnosticsSink(m_diagnosticsService.get());
    m_spectrumSnapshotAdapter =
        std::make_unique<SpectrumSnapshotAdapter>(&m_viewportModel,
                                                  &m_spectrumEnvelopeController,
                                                  m_dataIngestPipeline.get(),
                                                  m_diagnosticsService.get());

    m_hardwareProfile = makeDefaultHardwareProfile();
    if (m_udpSourceConfig) {
        AppState::instance().setMode(AppState::Mode::Combat);
    }
    selectBcoSource(AppState::instance().mode());
    m_bandConfigController =
        std::make_unique<BandConfigController>(&m_bandListModel,
                                               m_bcoControl.get(),
                                               m_diagnosticsService.get());

    m_bcoAcquisition = std::make_unique<pipeline::BcoAcquisitionSession>(
        m_bcoStreamSource.get(), m_dataIngestPipeline.get(), m_diagnosticsService.get());
    m_waterfallController = std::make_unique<WaterfallController>(&m_viewportModel,
                                                                  m_bcoAcquisition.get(),
                                                                  m_bandListModel.bandConfigs(),
                                                                  m_waterfallSessionStorage.get(),
                                                                  m_diagnosticsService.get(),
                                                                  WaterfallControllerConfig{},
                                                                  nullptr,
                                                                  nullptr,
                                                                  nullptr,
                                                                  m_dataIngestPipeline.get());
    m_recordingController = std::make_unique<RecordingController>(m_bcoControl.get(),
                                                                  &m_bandListModel,
                                                                  m_bandConfigController.get(),
                                                                  m_waterfallController.get(),
                                                                  &m_spectrumEnvelopeController,
                                                                  nullptr,
                                                                  m_diagnosticsService.get());
    m_scanRecordingControl =
        std::make_unique<WaterfallScanRecordingAdapter>(m_recordingController.get());

    m_scanController = std::make_unique<ScanController>(m_antennaControl.get(),
                                                        m_antennaAzimuthSource.get(),
                                                        nullptr,
                                                        nullptr,
                                                        m_bearingService.get(),
                                                        m_scanAcquisitionRecorder.get(),
                                                        m_waterfallController.get(),
                                                        m_scanRecordingControl.get(),
                                                        m_resultTableController.get(),
                                                        m_diagnosticsService.get());
    m_bearingSnapshotAdapter =
        std::make_unique<BearingSnapshotAdapter>(m_scanController.get(),
                                                 m_dataIngestPipeline.get(),
                                                 m_diagnosticsService.get());

    m_statusModel = std::make_unique<StatusModel>(m_diagnosticsService.get(),
                                                  &AppState::instance(),
                                                  m_waterfallController.get(),
                                                  m_recordingController.get(),
                                                  m_scanController.get(),
                                                  m_dataIngestPipeline.get());

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

    QObject::connect(m_bandConfigController.get(),
                     &BandConfigController::bandSettingsApplied,
                     m_waterfallController.get(),
                     [this](int) {
                         if (m_waterfallController) {
                             m_waterfallController->setBandConfigs(m_bandListModel.bandConfigs());
                         }
                         configureBcoStreamSource();
                     });
    QObject::connect(m_bandConfigController.get(),
                     &BandConfigController::generatorPulseSettingsApplied,
                     m_bandConfigController.get(),
                     [this](int) {
                         const auto pulseConfigs =
                             simulatorPulseConfigsFromBands(m_bandListModel);
                         m_hardwareProfile.simulatorLoadConfig.pulseBandConfigs = pulseConfigs;
                         if (auto* highLoadSource =
                                 dynamic_cast<hardware::HighLoadSimulatorBcoStreamSource*>(
                                     m_bcoStreamSource.get())) {
                             highLoadSource->setPulseBandConfigs(pulseConfigs);
                         } else if (auto* udp = dynamic_cast<hardware::UdpBcoStreamSource*>(m_bcoStreamSource.get())) {
                             udp->setPulseBandConfigs(pulseConfigs);
                         }
                     });
    m_modeConnection = QObject::connect(&AppState::instance(), &AppState::modeChanged,
                                        m_diagnosticsService.get(), [this](AppState::Mode mode) {
        if (!selectBcoSource(mode)) {
            AppState::instance().setMode(m_sourceMode);
        }
    });
    m_waterfallController->start();
    if (m_spectrumSnapshotAdapter) {
        m_spectrumSnapshotAdapter->start();
    }
    if (m_bearingSnapshotAdapter) {
        m_bearingSnapshotAdapter->start();
    }
    if (m_signalParameterSnapshotAdapter) {
        m_signalParameterSnapshotAdapter->start();
    }
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
    QObject::disconnect(m_modeConnection);
    if (m_spectrumSnapshotAdapter) {
        m_spectrumSnapshotAdapter->stop();
    }
    if (m_bearingSnapshotAdapter) {
        m_bearingSnapshotAdapter->stop();
    }
    if (m_signalParameterSnapshotAdapter) {
        m_signalParameterSnapshotAdapter->stop();
    }
    if (m_waterfallController) {
        m_waterfallController->stop();
    }
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
    profile.simulatorLoadConfig.profile =
        hardware::SimulatorLoadProfile::BaselineRawThroughput60MBps;
    profile.simulatorLoadConfig.pulseBandConfigs =
        simulatorPulseConfigsFromBands(m_bandListModel);
    return profile;
}

bool ApplicationBootstrap::selectBcoSource(AppState::Mode mode)
{
    if ((m_waterfallController && m_waterfallController->sourceActive())
        || (m_scanController && m_scanController->scanActive())
        || AppState::instance().modeChangeLocked()) {
        m_diagnosticsService->publish({infrastructure::DiagnosticSeverity::Warning, "Application",
            "Stop recording/scanning before switching BCO source", std::chrono::system_clock::now()});
        return false;
    }

    const auto pulseConfigs = simulatorPulseConfigsFromBands(m_bandListModel);
    m_hardwareProfile.bcoStreamConfig = makeBcoStreamConfig();
    m_hardwareProfile.simulatorLoadConfig.pulseBandConfigs = pulseConfigs;
    std::unique_ptr<hardware::IBcoStreamSource> source;
    std::unique_ptr<hardware::IBcoControl> control;
    std::string description;
    switch (mode) {
    case AppState::Mode::Test:
        source = hardware::DataSourceFactory::createHighLoadSimulatorBcoStreamSource(
            m_hardwareProfile, m_diagnosticsService.get(), m_antennaState.get());
        control = std::make_unique<hardware::HighLoadSimulatorBcoControl>(
            &m_hardwareProfile, source.get(), m_diagnosticsService.get());
        description = "BCO source: built-in generator, BaselineRawThroughput60MBps";
        break;
    case AppState::Mode::Combat: {
        const auto endpoint = m_udpSourceConfig.value_or(hardware::UdpBcoSourceConfig{});
        auto udp = std::make_unique<hardware::UdpBcoStreamSource>(
            endpoint, m_diagnosticsService.get(), m_antennaState.get());
        udp->setPulseBandConfigs(pulseConfigs);
        source = std::move(udp);
        control = std::make_unique<hardware::UdpBcoControl>(source.get());
        description = "BCO source: UDP generator " + endpoint.generatorHost + ":"
            + std::to_string(endpoint.generatorPort);
        break;
    }
    case AppState::Mode::Control:
        description = "Control mode: BCO acquisition disabled";
        break;
    default:
        return false;
    }

    auto configured = core::OperationResult::ok();
    if (mode != AppState::Mode::Control) {
        configured = source ? source->configure(m_hardwareProfile.bcoStreamConfig)
                            : core::OperationResult::failure("BCO source creation failed");
    }
    if (configured && m_bcoAcquisition) {
        configured = m_bcoAcquisition->setSource(source.get());
    }
    if (!configured) {
        m_diagnosticsService->publish({infrastructure::DiagnosticSeverity::Error, "Application",
            "BCO source switch failed: " + configured.message, std::chrono::system_clock::now()});
        return false;
    }

    // Keep the previous pair alive until all control-plane consumers are rebound.
    auto previousSource = std::move(m_bcoStreamSource);
    auto previousControl = std::move(m_bcoControl);
    m_bcoStreamSource = std::move(source);
    m_bcoControl = std::move(control);
    m_sourceMode = mode;
    if (m_bandConfigController) m_bandConfigController->setBcoControl(m_bcoControl.get());
    if (m_recordingController) m_recordingController->setBcoControl(m_bcoControl.get());
    m_diagnosticsService->publish({infrastructure::DiagnosticSeverity::Info, "Application",
        description, std::chrono::system_clock::now()});
    return true;
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
    BandConfigControllerQmlSingleton::instance = m_bandConfigController.get();
    DiagnosticsServiceQmlSingleton::instance = m_diagnosticsService.get();
    StatusModelQmlSingleton::instance = m_statusModel.get();
    ResultTableModelQmlSingleton::instance = m_resultTableModel.get();
}

} // namespace siriusscope::app
