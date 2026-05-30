#include "app/scancontroller.h"
#include "app/signalparametersnapshotadapter.h"
#include "pipeline/data_ingest_pipeline.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

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

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

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

pipeline::DataIngestPipelineConfig makePipelineConfig()
{
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{4, 16};
    config.queueCapacity = 4;
    config.diagnosticsPublishInterval = std::chrono::milliseconds{20};
    config.acceptingOnStart = true;
    config.signalParameters.estimatorConfig.samplePeriodNs = 1000;
    return config;
}

core::SignalSample makeSample(std::uint64_t sampleIndex)
{
    return core::SignalSample{
        sampleIndex,
        0,
        0,
        3'000'000'000LL,
        80,
        0,
    };
}

void testInactiveScanDropsSnapshotsBeforeControllerUpdate(TestRunner& test)
{
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig());
    app::ScanController scanController(nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr,
                                       nullptr);

    app::SignalParameterSnapshotAdapterConfig adapterConfig;
    adapterConfig.pollIntervalMs = 1;
    app::SignalParameterSnapshotAdapter adapter(&scanController,
                                                &dataPipeline,
                                                nullptr,
                                                adapterConfig);

    const auto started = dataPipeline.start();
    adapter.start();

    const std::vector<core::SignalSample> samples{
        makeSample(10),
        makeSample(11),
        makeSample(20),
        makeSample(21),
        makeSample(22),
    };
    const auto ingested = dataPipeline.ingestSamples(samples);
    const auto flushed = dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    processEventsFor(80);

    test.require(started.success, "data pipeline starts for signal parameter adapter test");
    test.require(ingested.success, "signal parameter adapter test ingests a signal block");
    test.require(flushed.success, "signal parameter adapter test flushes data plane");
    test.require(dataPipeline.latestSignalParameterSnapshot() != nullptr,
                 "data plane publishes latest signal parameter snapshot");
    test.require(adapter.lastSnapshotSequenceId() == 0,
                 "inactive scan drops signal parameter snapshot before controller update");
    test.require(scanController.collectedSignalSampleCount() == 0,
                 "inactive signal parameter adapter path does not update scan controller");

    adapter.stop();
    dataPipeline.stop();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testInactiveScanDropsSnapshotsBeforeControllerUpdate(test);

    return test.result();
}
