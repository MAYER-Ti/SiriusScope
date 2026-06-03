#include "app/applicationbootstrap.h"
#include "app/qmlsingletons.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/simulator/simulated_bco_payload_accounting.h"

#include <QCoreApplication>
#include <QStandardPaths>

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

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    TestRunner test;

    testBootstrapProvidesObjects(test);
    testBootstrapBaselinePipelineDisablesSignalParameterStage(test);
    testBootstrapWiresGeneratorPulseSettingsToSimulator(test);

    return test.result();
}
