#include "pipeline/data_ingest_pipeline.h"

#include <chrono>
#include <cmath>
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

pipeline::DataIngestPipelineConfig makeConfig()
{
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{4, 16};
    config.queueCapacity = 4;
    config.acceptingOnStart = true;
    config.signalParameters.estimatorConfig.samplePeriodNs = 1000;
    return config;
}

core::SignalSample sample(std::uint64_t sampleIndex)
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

bool nearly(double actual, double expected, double tolerance = 0.001)
{
    return std::abs(actual - expected) <= tolerance;
}

void testDataPipelinePublishesSignalParameterSnapshot(TestRunner& test)
{
    pipeline::DataIngestPipeline dataPipeline(makeConfig());

    const auto started = dataPipeline.start();
    const std::vector<core::SignalSample> samples{
        sample(10),
        sample(11),
        sample(20),
        sample(21),
        sample(22),
    };
    const auto ingested = dataPipeline.ingestSamples(samples);
    const auto flushed = dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto snapshot = dataPipeline.latestSignalParameterSnapshot();
    dataPipeline.stop();

    test.require(started.success, "data pipeline starts for signal parameter test");
    test.require(ingested.success, "data pipeline ingests signal parameter samples");
    test.require(flushed.success, "data pipeline flushes signal parameter samples");
    test.require(snapshot != nullptr,
                 "data pipeline publishes latest signal parameter snapshot");
    test.require(snapshot && snapshot->bands.size() == 1,
                 "data pipeline signal parameter snapshot contains one band");
    if (!snapshot || snapshot->bands.empty()) {
        return;
    }

    const auto& band0 = snapshot->bands.front();
    test.require(band0.pulseCount == 2,
                 "data pipeline snapshot counts two pulses");
    test.require(band0.pulseWidthUs && nearly(*band0.pulseWidthUs, 2.5),
                 "data pipeline snapshot calculates PW");
    test.require(band0.pulseRepetitionPeriodUs
                     && nearly(*band0.pulseRepetitionPeriodUs, 10.0),
                 "data pipeline snapshot calculates PRI");
}

void testDataPipelineForceSignalParameterSnapshotPublishesThrottledSamples(TestRunner& test)
{
    auto config = makeConfig();
    config.signalParameters.snapshotPeriod = std::chrono::hours{1};
    pipeline::DataIngestPipeline dataPipeline(config);

    const auto started = dataPipeline.start();
    const std::vector<core::SignalSample> firstBlock{
        sample(10),
        sample(11),
    };
    const std::vector<core::SignalSample> secondBlock{
        sample(20),
        sample(21),
        sample(22),
    };

    const auto firstIngested = dataPipeline.ingestSamples(firstBlock);
    const auto firstFlushed = dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto initialSnapshot = dataPipeline.latestSignalParameterSnapshot();
    const auto secondIngested = dataPipeline.ingestSamples(secondBlock);
    const auto secondFlushed = dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto throttledLatest = dataPipeline.latestSignalParameterSnapshot();
    const auto forcedSnapshot = dataPipeline.forceSignalParameterSnapshot();
    const auto latestSnapshot = dataPipeline.latestSignalParameterSnapshot();
    dataPipeline.stop();

    test.require(started.success, "data pipeline starts for forced snapshot test");
    test.require(firstIngested.success && secondIngested.success,
                 "data pipeline ingests forced snapshot blocks");
    test.require(firstFlushed.success && secondFlushed.success,
                 "data pipeline flushes forced snapshot blocks");
    test.require(initialSnapshot != nullptr,
                 "data pipeline publishes initial throttled snapshot");
    test.require(initialSnapshot && initialSnapshot->acceptedSampleCount == 2,
                 "initial throttled snapshot includes first block");
    test.require(throttledLatest && initialSnapshot
                     && throttledLatest->sequenceId == initialSnapshot->sequenceId,
                 "second block does not publish before signal parameter cadence");
    test.require(forcedSnapshot != nullptr,
                 "data pipeline forceSignalParameterSnapshot returns snapshot");
    test.require(forcedSnapshot && forcedSnapshot->acceptedSampleCount == 5,
                 "forced data pipeline snapshot includes throttled samples");
    test.require(latestSnapshot && forcedSnapshot
                     && latestSnapshot->sequenceId == forcedSnapshot->sequenceId,
                 "forced data pipeline snapshot is published as latest");
    test.require(forcedSnapshot && !forcedSnapshot->bands.empty()
                     && forcedSnapshot->bands.front().pulseCount == 2,
                 "forced data pipeline snapshot contains complete pulse summary");
}

} // namespace

int main()
{
    TestRunner test;

    testDataPipelinePublishesSignalParameterSnapshot(test);
    testDataPipelineForceSignalParameterSnapshotPublishesThrottledSamples(test);

    return test.result();
}
