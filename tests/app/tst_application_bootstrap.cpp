#include "app/applicationbootstrap.h"
#include "app/qmlsingletons.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/simulator/simulated_bco_payload_accounting.h"
#include "hardware/udp/udp_bco_control.h"

#include <QCoreApplication>
#include <QStandardPaths>
#include <QProcess>
#include <QElapsedTimer>
#include <array>
#include <thread>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

std::vector<siriusscope::core::SignalSample> makeBaselineSamples(
    const siriusscope::core::BandConfig& band,
    std::size_t sampleCount)
{
    std::vector<siriusscope::core::SignalSample> samples;
    samples.reserve(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        samples.push_back(siriusscope::core::SignalSample{
            static_cast<std::uint64_t>(index),
            band.bandIndex,
            0,
            band.centerFrequencyHz,
            90,
            static_cast<int>(index % 2),
        });
    }
    return samples;
}

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
    test.require(bootstrap.spectrumEnvelopeController() != nullptr,
                 "bootstrap provides spectrum envelope controller");
    test.require(bootstrap.spectrumSnapshotAdapter() != nullptr,
                 "bootstrap provides spectrum snapshot adapter");
    test.require(bootstrap.bearingSnapshotAdapter() != nullptr,
                 "bootstrap provides bearing snapshot adapter");
    test.require(bootstrap.signalParameterSnapshotAdapter() == nullptr,
                 "bootstrap does not create signal parameter snapshot adapter in baseline");
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
    test.require(bootstrap.recordingController() != nullptr,
                 "bootstrap provides recording controller");
    test.require(bootstrap.scanController() != nullptr,
                 "bootstrap provides scan controller");
    test.require(bootstrap.resultTableModel() != nullptr,
                 "bootstrap provides result table model");
    test.require(bootstrap.resultTableController() != nullptr,
                 "bootstrap provides result table controller");
    test.require(bootstrap.bearingFrameBus() != nullptr,
                 "bootstrap provides bearing frame bus");
    test.require(bootstrap.signalSampleBus() != nullptr,
                 "bootstrap provides signal sample bus");
    test.require(bootstrap.dataIngestPipeline() != nullptr,
                 "bootstrap provides high-load data ingest pipeline");
    test.require(bootstrap.scanAcquisitionRecorder() != nullptr,
                 "bootstrap provides scan acquisition recorder");
    test.require(bootstrap.processingFlushControl() != nullptr,
                 "bootstrap provides processing flush control");
    test.require(bootstrap.scanRecordingControl() != nullptr,
                 "bootstrap provides scan recording control");
    test.require(bootstrap.resultTableSink() != nullptr,
                 "bootstrap provides result table sink");
    test.require(bootstrap.resultTableSink() == bootstrap.resultTableController(),
                 "bootstrap uses result table controller as production sink");
    test.require(bootstrap.waterfallStorage() != nullptr,
                 "bootstrap provides waterfall storage placeholder");
    test.require(bootstrap.bcoControl() != nullptr,
                 "bootstrap provides BCO control");
    test.require(bootstrap.bcoStreamSource() != nullptr,
                 "bootstrap provides BCO stream source");
    test.require(dynamic_cast<siriusscope::hardware::HighLoadSimulatorBcoStreamSource*>(
                     bootstrap.bcoStreamSource()) != nullptr,
                 "bootstrap uses high-load simulator BCO stream source");
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
    test.require(siriusscope::app::RecordingControllerQmlSingleton::instance
                     == bootstrap.recordingController(),
                 "bootstrap registers recording controller singleton");
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
    test.require(siriusscope::app::SpectrumEnvelopeControllerQmlSingleton::instance
                     == bootstrap.spectrumEnvelopeController(),
                 "bootstrap registers spectrum envelope singleton");
    test.require(siriusscope::app::DiagnosticsServiceQmlSingleton::instance
                     == bootstrap.diagnosticsService(),
                 "bootstrap registers diagnostics service singleton");
    test.require(siriusscope::app::StatusModelQmlSingleton::instance
                     == bootstrap.statusModel(),
                 "bootstrap registers status model singleton");
    test.require(siriusscope::app::ResultTableModelQmlSingleton::instance
                     == bootstrap.resultTableModel(),
                 "bootstrap registers result table model singleton");
}

void testBootstrapBaselinePipelineDisablesSignalParameterStage(TestRunner& test)
{
    siriusscope::app::ApplicationBootstrap bootstrap;
    auto* pipeline = bootstrap.dataIngestPipeline();
    auto* bandModel = bootstrap.bandListModel();
    const auto* band = bandModel ? bandModel->bandAt(0) : nullptr;

    test.require(pipeline != nullptr, "bootstrap provides baseline data pipeline");
    test.require(band != nullptr, "bootstrap provides a band for baseline smoke");
    if (!pipeline || !band) {
        return;
    }

    const auto target = siriusscope::hardware::baselineRawThroughput60MbpsTarget();
    const auto sampleCount = siriusscope::hardware::samplesPerBatchForTarget(target);
    auto samples = makeBaselineSamples(band->config, sampleCount);
    siriusscope::pipeline::SignalBlockMetadata metadata;
    metadata.firstSampleIndex = 0;
    metadata.lastSampleIndex = static_cast<std::uint64_t>(samples.size() - 1);
    metadata.producedAt = std::chrono::steady_clock::now();
    metadata.antennaAzimuthDeg = 45.0;

    pipeline->setAccepting(true);
    const auto ingested = pipeline->ingestSamples(samples, metadata);
    const auto flushed = pipeline->flushProcessing(std::chrono::milliseconds{5000});
    const auto metrics = pipeline->metricsSnapshot();
    const auto spectrumSnapshot = pipeline->latestSpectrumSnapshot();
    const auto bearingSnapshot = pipeline->latestBearingSnapshot();
    const auto signalParameterSnapshot = pipeline->latestSignalParameterSnapshot();
    pipeline->setAccepting(false);

    test.require(sampleCount == 37'120, "baseline smoke uses 37120 samples per batch");
    test.require(ingested.success, "baseline pipeline accepts one full baseline block");
    test.require(flushed.success, "baseline pipeline flushes one full baseline block");
    test.require(metrics.parallelFanOutBlocks > 0,
                 "baseline pipeline uses parallel fan-out");
    test.require(metrics.inputSamples == sampleCount,
                 "baseline pipeline records full baseline input block");
    test.require(metrics.processedSamples == sampleCount,
                 "baseline pipeline processes full baseline input block");
    test.require(metrics.waterfallStageProcessedBlocks > 0,
                 "baseline pipeline processes waterfall stage");
    test.require(metrics.spectrumStageProcessedBlocks > 0,
                 "baseline pipeline processes spectrum stage");
    test.require(metrics.bearingStageProcessedBlocks > 0,
                 "baseline pipeline processes bearing stage");
    test.require(metrics.signalParameterStageProcessedBlocks == 0,
                 "baseline pipeline sends no blocks to signal parameter stage");
    test.require(metrics.producedSignalParameterSnapshots == 0,
                 "baseline pipeline produces no signal parameter snapshots");
    test.require(spectrumSnapshot != nullptr,
                 "baseline pipeline publishes spectrum snapshot");
    test.require(bearingSnapshot != nullptr,
                 "baseline pipeline publishes bearing snapshot");
    test.require(signalParameterSnapshot == nullptr,
                 "baseline pipeline keeps signal parameter snapshot absent");
}

void testBootstrapWiresGeneratorPulseSettingsToSimulator(TestRunner& test)
{
    siriusscope::app::ApplicationBootstrap bootstrap;
    auto* streamSource =
        dynamic_cast<siriusscope::hardware::HighLoadSimulatorBcoStreamSource*>(
            bootstrap.bcoStreamSource());

    test.require(streamSource != nullptr,
                 "bootstrap uses high-load simulator BCO stream source");
    if (!streamSource) {
        return;
    }

    const bool applied =
        bootstrap.bandConfigController()->applyGeneratorPulseSettings(1, 200000.0, 25000.0);
    test.require(applied, "generator pulse settings apply through bootstrap controller");

    const auto configs = streamSource->pulseBandConfigs();
    const auto band1 = std::find_if(configs.begin(), configs.end(), [](const auto& config) {
        return config.bandIndex == 1;
    });

    test.require(band1 != configs.end(), "high-load simulator pulse configs contain updated band");
    if (band1 != configs.end()) {
        test.require(band1->pulsePeriodUs == 200000.0,
                     "simulator receives updated generator pulse period");
        test.require(band1->pulseWidthUs == 25000.0,
                     "simulator receives updated generator pulse width");
    }
}

void testBootstrapSelectsUdp(TestRunner& test)
{
    siriusscope::hardware::UdpBcoSourceConfig endpoint;
    endpoint.bindPort = 0;
    siriusscope::app::ApplicationBootstrap bootstrap(endpoint);
    test.require(AppState::instance().mode() == AppState::Mode::Combat,
                 "explicit UDP endpoint selects Hardware in UI as well as transport");
    test.require(dynamic_cast<siriusscope::hardware::UdpBcoStreamSource*>(bootstrap.bcoStreamSource()) != nullptr,
                 "explicit UDP endpoint selects the network receiver");
    test.require(dynamic_cast<siriusscope::hardware::UdpBcoControl*>(bootstrap.bcoControl()) != nullptr,
                 "UDP source uses matching recording control adapter");
    test.require(bootstrap.bandConfigController()->applyGeneratorPulseSettings(1, 200000.0, 25000.0),
                 "UDP source accepts generator settings through application controller");
}


void testBootstrapSwitchesSourcesAndPreservesControllers(TestRunner& test)
{
    auto& state = AppState::instance();
    state.setModeChangeLocked(false);
    state.setMode(AppState::Mode::Test);
    siriusscope::app::ApplicationBootstrap bootstrap;
    bootstrap.registerQmlSingletons();
    auto* waterfall = bootstrap.waterfallController();
    auto* recording = bootstrap.recordingController();
    auto* bands = bootstrap.bandListModel();
    auto* bandController = bootstrap.bandConfigController();
    auto* pipeline = bootstrap.dataIngestPipeline();
    test.require(bandController->applyBandSettings(0, 3'050'000'000.0, 400'000'000.0,
                                                 40.0, 10, 20, QStringLiteral("vertical")),
                 "band configuration applies before source switch");
    test.require(bandController->applyGeneratorPulseSettings(1, 200000.0, 25000.0),
                 "pulse configuration applies before source switch");
    for (int cycle = 0; cycle < 2; ++cycle) {
        state.setMode(AppState::Mode::Combat);
        test.require(dynamic_cast<siriusscope::hardware::UdpBcoStreamSource*>(
                         bootstrap.bcoStreamSource()) != nullptr,
                     "hardware mode selects UDP receiver without CLI options");
        test.require(dynamic_cast<siriusscope::hardware::UdpBcoControl*>(
                         bootstrap.bcoControl()) != nullptr,
                     "hardware mode selects matching UDP control");
        test.require(!waterfall->sourceActive(), "mode switch does not start recording");
        state.setModeChangeLocked(true);
        state.setMode(AppState::Mode::Test);
        test.require(state.mode() == AppState::Mode::Combat, "mode lock rejects source switch");
        state.setModeChangeLocked(false);
        state.setMode(AppState::Mode::Test);
        auto* generator = dynamic_cast<siriusscope::hardware::HighLoadSimulatorBcoStreamSource*>(
            bootstrap.bcoStreamSource());
        test.require(generator != nullptr, "generator mode restores built-in source");
        if (generator) {
            const auto pulses = generator->pulseBandConfigs();
            const auto updated = std::find_if(pulses.begin(), pulses.end(), [](const auto& value) {
                return value.bandIndex == 1;
            });
            test.require(updated != pulses.end() && updated->pulsePeriodUs == 200000.0
                             && updated->pulseWidthUs == 25000.0,
                         "generator pulse settings survive source switching");
        }
        const auto* band = bands->bandState(0);
        test.require(band && band->config.centerFrequencyHz == 3'050'000'000LL
                         && band->config.widthHz == 400'000'000LL
                         && band->thresholdAmplitude == 40.0
                         && band->inputAttenuatorDb == 10 && band->outputAttenuatorDb == 20
                         && band->polarization == QStringLiteral("vertical"),
                     "receiver settings survive source switching");
        test.require(bootstrap.waterfallController() == waterfall
                         && bootstrap.recordingController() == recording
                         && bootstrap.bandListModel() == bands
                         && bootstrap.bandConfigController() == bandController
                         && bootstrap.dataIngestPipeline() == pipeline,
                     "mode changes preserve controller and pipeline identities");
        test.require(siriusscope::app::WaterfallControllerQmlSingleton::instance == waterfall
                         && siriusscope::app::RecordingControllerQmlSingleton::instance == recording
                         && siriusscope::app::BandConfigControllerQmlSingleton::instance == bandController,
                     "mode changes preserve QML singleton identities");
    }
}

void testHardwareWaitsForExternalProcess(TestRunner& test, const QString& generatorPath)
{
    auto& state = AppState::instance();
    state.setModeChangeLocked(false);
    state.setMode(AppState::Mode::Test);
    bco_generator::host::Endpoint quietAddress;
    test.require(bco_generator::host::ipv4Endpoint("127.0.0.1", 0, quietAddress),
                 "quiet generator endpoint parses");
    bco_generator::host::UdpSocket quietEndpoint;
    std::string error;
    const bool opened = quietEndpoint.open(quietAddress, error);
    test.require(opened, "reserve endpoint without producer");
    if (!opened) return;
    siriusscope::hardware::UdpBcoSourceConfig endpoint;
    endpoint.generatorPort = quietEndpoint.localPort();
    endpoint.bindPort = 0;
    siriusscope::app::ApplicationBootstrap bootstrap(endpoint);
    state.setMode(AppState::Mode::Test);
    state.setMode(AppState::Mode::Combat);
    auto* source = dynamic_cast<siriusscope::hardware::UdpBcoStreamSource*>(bootstrap.bcoStreamSource());
    auto* recording = bootstrap.recordingController();
    test.require(source != nullptr, "hardware restores configured UDP source");
    if (!source) return;
    recording->startRecording();
    test.require(recording->recordingActive() && bootstrap.waterfallController()->sourceActive(),
                 "hardware recording waits while generator is absent");
    std::array<std::byte, 2048> datagram{};
    bco_generator::host::Endpoint sender;
    test.require(quietEndpoint.receive(datagram, sender, 1000) > 0,
                 "source subscribes to configured external endpoint");
    state.setMode(AppState::Mode::Test);
    test.require(state.mode() == AppState::Mode::Combat && bootstrap.bcoStreamSource() == source,
                 "recording prevents source switching");
    test.require(!recording->setBcoControl(nullptr), "active control cannot be replaced");
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    QCoreApplication::processEvents();
    test.require(source->metrics().producedSamples == 0
                     && bootstrap.dataIngestPipeline()->metricsSnapshot().inputSamples == 0,
                 "absent external generator never produces fallback samples");
    test.require(bootstrap.statusModel()->bcoValue() == QStringLiteral("ожидание данных"),
                 "hardware honestly reports waiting before any DATA");

    quietEndpoint.close();
    QProcess generator;
    generator.start(generatorPath, {QStringLiteral("--bind"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QString::number(endpoint.generatorPort),
        QStringLiteral("--rate"), QStringLiteral("1280")});
    test.require(generator.waitForStarted(2000), "separate generator starts after receiver");
    QElapsedTimer timeout;
    timeout.start();
    while (timeout.elapsed() < 5000 &&
           (bootstrap.dataIngestPipeline()->metricsSnapshot().processedSamples == 0
            || bootstrap.dataIngestPipeline()->waterfallRowQueueMetrics().pushedRows < 3
            || bootstrap.statusModel()->bcoValue() != QStringLiteral("приём данных"))) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    test.require(source->metrics().producedSamples > 0
                     && bootstrap.dataIngestPipeline()->metricsSnapshot().processedSamples > 0,
                 "late external process supplies samples through bootstrap to pipeline");
    test.require(bootstrap.dataIngestPipeline()->waterfallRowQueueMetrics().pushedRows >= 3
                     && bootstrap.dataIngestPipeline()->latestSpectrumSnapshot() != nullptr,
                 "default external rate produces live waterfall and spectrum before stop");
    test.require(bootstrap.statusModel()->bcoValue() == QStringLiteral("приём данных"),
                 "valid network data updates hardware status");
    recording->stopRecording();
    generator.terminate();
    if (!generator.waitForFinished(2000)) { generator.kill(); generator.waitForFinished(); }
    test.require(!state.modeChangeLocked() && !bootstrap.waterfallController()->sourceActive()
                     && recording->canStartRecording(), "stop releases source and mode lock");
    state.setMode(AppState::Mode::Test);
    auto* builtIn = dynamic_cast<siriusscope::hardware::HighLoadSimulatorBcoStreamSource*>(
        bootstrap.bcoStreamSource());
    test.require(builtIn != nullptr, "generator is restored after network recording");
    recording->startRecording();
    timeout.restart();
    while (builtIn && builtIn->metrics().producedSamples == 0 && timeout.elapsed() < 2000) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    test.require(recording->recordingActive() && builtIn && builtIn->metrics().producedSamples > 0,
                 "restored built-in source generates data without external process");
    recording->stopRecording();
}

void testControlModeDisablesAcquisition(TestRunner& test)
{
    auto& state = AppState::instance();
    state.setModeChangeLocked(false);
    state.setMode(AppState::Mode::Control);
    siriusscope::app::ApplicationBootstrap bootstrap;
    auto* recording = bootstrap.recordingController();
    test.require(!recording->canStartRecording() && !bootstrap.bcoStreamSource(),
                 "control mode initializes without acquisition");
    test.require(bootstrap.statusModel()->bcoValue() == QStringLiteral("приём отключён"),
                 "initial Control status reflects disabled acquisition");
    recording->startRecording();
    bootstrap.scanController()->startScan(10.0, 20.0, 5.0);
    test.require(!recording->recordingActive() && !bootstrap.waterfallController()->sourceActive()
                     && !bootstrap.scanController()->scanActive() && !state.modeChangeLocked(),
                 "control mode rejects recording/scanning and allows mode selection");
    state.setMode(AppState::Mode::Test);
    test.require(recording->canStartRecording()
                     && dynamic_cast<siriusscope::hardware::HighLoadSimulatorBcoStreamSource*>(
                         bootstrap.bcoStreamSource()) != nullptr,
                 "leaving control restores built-in acquisition");
    state.setMode(AppState::Mode::Control);
    test.require(!recording->canStartRecording() && !bootstrap.bcoStreamSource(),
                 "switching to control disables acquisition");
    state.setMode(AppState::Mode::Test);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    TestRunner test;
    AppState::instance().setModeChangeLocked(false);
    AppState::instance().setMode(AppState::Mode::Test);

    testBootstrapProvidesObjects(test);
    testBootstrapBaselinePipelineDisablesSignalParameterStage(test);
    testBootstrapWiresGeneratorPulseSettingsToSimulator(test);
    testBootstrapSelectsUdp(test);
    testBootstrapSwitchesSourcesAndPreservesControllers(test);
    test.require(argc > 1, "external generator executable supplied by CTest");
    if (argc > 1) testHardwareWaitsForExternalProcess(test, QString::fromLocal8Bit(argv[1]));
    testControlModeDisablesAcquisition(test);

    return test.result();
}
