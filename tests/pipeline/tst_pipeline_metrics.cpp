#include "pipeline/pipeline_metrics.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

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

    void requireNear(double actual,
                     double expected,
                     double tolerance,
                     const std::string& message)
    {
        require(std::abs(actual - expected) <= tolerance, message);
    }

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

pipeline::PipelineMetricsSnapshot snapshotFor(const pipeline::PipelineMetrics& metrics)
{
    return metrics.snapshot({}, {});
}

void testLatencyStatsAverageAndMax(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordWaterfallAggregationLatency(std::chrono::milliseconds{2});
    metrics.recordWaterfallAggregationLatency(std::chrono::milliseconds{4});

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.waterfallAggregationLatency.count == 2,
                 "latency stats count records");
    test.requireNear(snapshot.waterfallAggregationLatency.averageMs(),
                     3.0,
                     0.0001,
                     "latency stats average is calculated");
    test.requireNear(snapshot.waterfallAggregationLatency.maxMs,
                     4.0,
                     0.0001,
                     "latency stats max is tracked");
}

void testAverageSamplesPerProcessedBlock(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordProcessBlockLatency(std::chrono::milliseconds{1}, 100);
    metrics.recordProcessBlockLatency(std::chrono::milliseconds{2}, 300);

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.processBlockLatency.count == 2,
                 "process block latency count records");
    test.require(snapshot.processedBlockSamplesTotal == 400,
                 "processed block sample total is tracked");
    test.requireNear(snapshot.averageSamplesPerProcessedBlock,
                     200.0,
                     0.0001,
                     "average samples per processed block is calculated");
    test.requireNear(snapshot.processBlockLatency.averageMs(),
                     1.5,
                     0.0001,
                     "process block latency average is calculated");
    test.requireNear(snapshot.processBlockLatency.maxMs,
                     2.0,
                     0.0001,
                     "process block latency max is tracked");
}

void testResetClearsLatencyStats(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordProcessBlockLatency(std::chrono::milliseconds{1}, 100);
    metrics.recordSpectrumSnapshotPublishLatency(std::chrono::milliseconds{3});
    metrics.reset();

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.processBlockLatency.count == 0,
                 "reset clears process block latency count");
    test.require(snapshot.processedBlockSamplesTotal == 0,
                 "reset clears processed block sample total");
    test.requireNear(snapshot.averageSamplesPerProcessedBlock,
                     0.0,
                     0.0001,
                     "reset clears average samples per processed block");
    test.require(snapshot.spectrumSnapshotPublishLatency.count == 0,
                 "reset clears publish latency count");
}

} // namespace

int main()
{
    TestRunner test;

    testLatencyStatsAverageAndMax(test);
    testAverageSamplesPerProcessedBlock(test);
    testResetClearsLatencyStats(test);

    return test.result();
}
