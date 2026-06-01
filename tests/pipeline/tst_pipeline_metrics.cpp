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

void testSignalParameterMicroBreakdownLatencyStats(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordSignalParameterIngestLatency(std::chrono::milliseconds{10});
    metrics.recordSignalParameterIngestLatency(std::chrono::milliseconds{30});
    metrics.recordSignalParameterSnapshotDecisionLatency(std::chrono::milliseconds{1});
    metrics.recordSignalParameterFinalizeLatency(std::chrono::milliseconds{4});
    metrics.recordSignalParameterSnapshotBuildLatency(std::chrono::milliseconds{6});

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.signalParameterIngestLatency.count == 2,
                 "signal parameter ingest latency count records");
    test.requireNear(snapshot.signalParameterIngestLatency.averageMs(),
                     20.0,
                     0.0001,
                     "signal parameter ingest latency average is calculated");
    test.requireNear(snapshot.signalParameterIngestLatency.maxMs,
                     30.0,
                     0.0001,
                     "signal parameter ingest latency max is tracked");
    test.require(snapshot.signalParameterSnapshotDecisionLatency.count == 1,
                 "signal parameter snapshot decision latency count records");
    test.requireNear(snapshot.signalParameterFinalizeLatency.averageMs(),
                     4.0,
                     0.0001,
                     "signal parameter finalize latency average is calculated");
    test.requireNear(snapshot.signalParameterSnapshotBuildLatency.maxMs,
                     6.0,
                     0.0001,
                     "signal parameter snapshot build latency max is tracked");
}

void testSignalParameterFastPathCounter(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordSignalParameterTrustedFixedBandFastPathBlock();
    metrics.recordSignalParameterTrustedFixedBandFastPathBlock();

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.signalParameterTrustedFixedBandFastPathBlocks == 2,
                 "signal parameter trusted fixed-band fast-path blocks are counted");
}

void testResetClearsLatencyStats(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordProcessBlockLatency(std::chrono::milliseconds{1}, 100);
    metrics.recordSpectrumSnapshotPublishLatency(std::chrono::milliseconds{3});
    metrics.recordSignalParameterFinalizeLatency(std::chrono::milliseconds{4});
    metrics.recordSignalParameterTrustedFixedBandFastPathBlock();
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
    test.require(snapshot.signalParameterFinalizeLatency.count == 0,
                 "reset clears signal parameter finalize latency count");
    test.require(snapshot.signalParameterTrustedFixedBandFastPathBlocks == 0,
                 "reset clears signal parameter fast-path block counter");
}

} // namespace

int main()
{
    TestRunner test;

    testLatencyStatsAverageAndMax(test);
    testAverageSamplesPerProcessedBlock(test);
    testSignalParameterMicroBreakdownLatencyStats(test);
    testSignalParameterFastPathCounter(test);
    testResetClearsLatencyStats(test);

    return test.result();
}
