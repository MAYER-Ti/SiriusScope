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

} // namespace

int main()
{
    TestRunner test;

    testDataPipelinePublishesSignalParameterSnapshot(test);

    return test.result();
}
