#include "app/frequencyviewportmodel.h"
#include "app/spectrumenvelopecontroller.h"
#include "app/spectrumsnapshotadapter.h"
#include "pipeline/data_ingest_pipeline.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QVariantList>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace siriusscope;

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

void processEventsFor(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

double maxControllerSample(const app::SpectrumEnvelopeController& controller)
{
    const QVariantList samples = controller.envelopeSamples();
    double maxValue = 0.0;
    for (const auto& sample : samples) {
        maxValue = std::max(maxValue, sample.toDouble());
    }
    return maxValue;
}

core::SignalSample makeSample(std::uint64_t sampleIndex,
                              std::int64_t frequencyHz,
                              int amplitude,
                              int beamIndex)
{
    return core::SignalSample{
        sampleIndex,
        0,
        0,
        frequencyHz,
        amplitude,
        beamIndex,
    };
}

pipeline::DataIngestPipelineConfig makePipelineConfig()
{
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{4, 16};
    config.queueCapacity = 4;
    config.diagnosticsPublishInterval = std::chrono::milliseconds{20};
    config.acceptingOnStart = true;
    config.spectrum.renderBinCount = 64;
    config.spectrum.snapshotPeriodNs = 20'000'000;
    return config;
}

void testSnapshotAdapterFeedsEnvelopeController(TestRunner& test)
{
    FrequencyViewportModel viewport;
    viewport.setViewport(viewport.globalMinHz(), viewport.globalMaxHz(), QStringLiteral("test"));

    app::SpectrumEnvelopeControllerConfig envelopeConfig;
    envelopeConfig.binCount = 64;
    envelopeConfig.publishIntervalMs = 1;
    app::SpectrumEnvelopeController envelope(envelopeConfig);

    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig());

    app::SpectrumSnapshotAdapterConfig adapterConfig;
    adapterConfig.pollIntervalMs = 1;
    adapterConfig.outputBinCount = 64;
    app::SpectrumSnapshotAdapter adapter(&viewport,
                                         &envelope,
                                         &dataPipeline,
                                         nullptr,
                                         adapterConfig);

    const auto started = dataPipeline.start();
    adapter.start();

    const std::vector<core::SignalSample> samples{
        makeSample(0, 3'000'000'000LL, 100, 0),
        makeSample(1, 3'000'000'000LL, 80, 1),
    };
    const auto ingested = dataPipeline.ingestSamples(samples);
    const auto flushed = dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    processEventsFor(80);

    test.require(started.success, "data pipeline starts for spectrum adapter test");
    test.require(ingested.success, "spectrum adapter test ingests a signal block");
    test.require(flushed.success, "spectrum adapter test flushes data plane");
    test.require(dataPipeline.latestSpectrumSnapshot() != nullptr,
                 "data plane publishes latest spectrum snapshot");
    test.require(adapter.lastSnapshotSequenceId() > 0,
                 "spectrum adapter polls latest immutable snapshot");
    test.require(maxControllerSample(envelope) == 100.0,
                 "spectrum adapter feeds envelope controller from snapshot bins");

    viewport.setViewport(4'000'000'000.0, 5'000'000'000.0, QStringLiteral("test"));
    processEventsFor(80);
    test.require(maxControllerSample(envelope) == 0.0,
                 "viewport change re-adapts latest snapshot without raw samples");

    adapter.stop();
    dataPipeline.stop();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testSnapshotAdapterFeedsEnvelopeController(test);

    return test.result();
}
