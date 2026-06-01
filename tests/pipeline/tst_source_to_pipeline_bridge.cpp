#include "hardware/interfaces/bco_stream_source.h"
#include "pipeline/data_ingest_pipeline.h"
#include "pipeline/source_to_pipeline_bridge.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
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

class RecordingDiagnosticsSink final : public infrastructure::IDiagnosticsSink
{
public:
    void publish(const infrastructure::DiagnosticEvent& event) override
    {
        events.push_back(event);
    }

    std::vector<infrastructure::DiagnosticEvent> events;
};

pipeline::DataIngestPipelineConfig makeConfig(bool acceptingOnStart = true)
{
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{4, 16};
    config.queueCapacity = 4;
    config.acceptingOnStart = acceptingOnStart;
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

hardware::IBcoStreamSource::SampleBlockPtr makeBlock(std::uint64_t firstSampleIndex,
                                                     std::size_t sampleCount)
{
    auto block = std::make_shared<hardware::BcoSampleBlock>();
    block->samples.reserve(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        block->samples.push_back(makeSample(firstSampleIndex + index));
    }
    block->stats.firstSampleIndex = firstSampleIndex;
    block->stats.lastSampleIndex =
        sampleCount > 0 ? firstSampleIndex + sampleCount - 1 : firstSampleIndex;
    block->stats.sampleCount = static_cast<std::uint64_t>(sampleCount);
    block->stats.packetCount = 1;
    block->stats.producedAt = std::chrono::steady_clock::now();
    block->stats.antennaAzimuthDeg = 45.0;
    return block;
}

void testStartFailsWithoutPipeline(TestRunner& test)
{
    RecordingDiagnosticsSink diagnostics;
    pipeline::SourceToPipelineBridge bridge(nullptr, {}, &diagnostics);

    const auto started = bridge.start();

    test.require(!started.success, "bridge start fails without pipeline");
    test.require(!diagnostics.events.empty(),
                 "bridge publishes diagnostic when pipeline is missing");
    test.require(!diagnostics.events.empty()
                     && diagnostics.events.front().severity
                         == infrastructure::DiagnosticSeverity::Error,
                 "missing pipeline diagnostic is an error");
}

void testSubmitBeforeStartDropsBlock(TestRunner& test)
{
    pipeline::DataIngestPipeline dataPipeline(makeConfig());
    pipeline::SourceToPipelineBridge bridge(&dataPipeline);

    bridge.submit(makeBlock(10, 3));
    const auto metrics = bridge.metrics();

    test.require(metrics.receivedBlocks == 1,
                 "bridge counts block submitted before start");
    test.require(metrics.receivedSamples == 3,
                 "bridge counts samples submitted before start");
    test.require(metrics.droppedBlocks == 1,
                 "bridge drops block submitted before start");
    test.require(metrics.droppedSamples == 3,
                 "bridge counts dropped samples before start");
    test.require(metrics.enqueuedBlocks == 0,
                 "bridge does not enqueue before start");
}

void testBridgeIngestsBlockIntoPipeline(TestRunner& test)
{
    pipeline::DataIngestPipeline dataPipeline(makeConfig());
    pipeline::SourceToPipelineBridge bridge(&dataPipeline);

    const auto pipelineStarted = dataPipeline.start();
    const auto bridgeStarted = bridge.start();
    bridge.submit(makeBlock(20, 5));
    const auto bridgeFlushed = bridge.flush(std::chrono::milliseconds{1500});
    const auto pipelineFlushed =
        dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto bridgeMetrics = bridge.metrics();
    const auto pipelineMetrics = dataPipeline.metricsSnapshot();
    bridge.stop();
    dataPipeline.stop();

    test.require(pipelineStarted.success, "data pipeline starts for bridge ingest test");
    test.require(bridgeStarted.success, "bridge starts for ingest test");
    test.require(bridgeFlushed.success, "bridge flush waits for RX delivery");
    test.require(pipelineFlushed.success,
                 "data pipeline flushes block delivered by bridge");
    test.require(bridgeMetrics.ingestedBlocks == 1,
                 "bridge counts successfully ingested block");
    test.require(bridgeMetrics.ingestedSamples == 5,
                 "bridge counts successfully ingested samples");
    test.require(bridgeMetrics.rejectedBlocks == 0,
                 "bridge reports no rejected blocks on happy path");
    test.require(pipelineMetrics.inputSamples == 5,
                 "pipeline receives samples delivered by bridge");
    test.require(pipelineMetrics.processedSamples == pipelineMetrics.inputSamples,
                 "pipeline processes samples delivered by bridge");
}

void testBridgeCountsPipelineRejection(TestRunner& test)
{
    pipeline::DataIngestPipeline dataPipeline(makeConfig(false));
    pipeline::SourceToPipelineBridge bridge(&dataPipeline);

    const auto pipelineStarted = dataPipeline.start();
    const auto bridgeStarted = bridge.start();
    bridge.submit(makeBlock(30, 4));
    const auto bridgeFlushed = bridge.flush(std::chrono::milliseconds{1500});
    const auto bridgeMetrics = bridge.metrics();
    bridge.stop();
    dataPipeline.stop();

    test.require(pipelineStarted.success, "data pipeline starts for rejection test");
    test.require(bridgeStarted.success, "bridge starts for rejection test");
    test.require(bridgeFlushed.success,
                 "bridge flush completes after rejected pipeline ingest");
    test.require(bridgeMetrics.rejectedBlocks == 1,
                 "bridge counts pipeline rejected block");
    test.require(bridgeMetrics.rejectedSamples == 4,
                 "bridge counts pipeline rejected samples");
    test.require(bridgeMetrics.ingestedBlocks == 0,
                 "bridge does not count rejected block as ingested");
}

} // namespace

int main()
{
    TestRunner test;

    testStartFailsWithoutPipeline(test);
    testSubmitBeforeStartDropsBlock(test);
    testBridgeIngestsBlockIntoPipeline(test);
    testBridgeCountsPipelineRejection(test);

    return test.result();
}
