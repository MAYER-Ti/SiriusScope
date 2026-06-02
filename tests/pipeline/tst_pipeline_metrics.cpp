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

void testSpectrumMicroBreakdownLatencyStats(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordSpectrumSampleLoopLatency(std::chrono::milliseconds{2});
    metrics.recordSpectrumSampleLoopLatency(std::chrono::milliseconds{4});
    metrics.recordSpectrumWindowCalculationLatency(std::chrono::milliseconds{1});
    metrics.recordSpectrumBinCalculationLatency(std::chrono::milliseconds{3});
    metrics.recordSpectrumBinUpdateLatency(std::chrono::milliseconds{5});
    metrics.recordSpectrumBandSummaryUpdateLatency(std::chrono::milliseconds{7});
    metrics.recordSpectrumCloseWindowLatency(std::chrono::milliseconds{9});
    metrics.recordSpectrumSnapshotBuildLatency(std::chrono::milliseconds{11});

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.spectrumSampleLoopLatency.count == 2,
                 "spectrum sample loop latency count records");
    test.requireNear(snapshot.spectrumSampleLoopLatency.averageMs(),
                     3.0,
                     0.0001,
                     "spectrum sample loop latency average is calculated");
    test.requireNear(snapshot.spectrumSampleLoopLatency.maxMs,
                     4.0,
                     0.0001,
                     "spectrum sample loop latency max is tracked");
    test.requireNear(snapshot.spectrumWindowCalculationLatency.averageMs(),
                     1.0,
                     0.0001,
                     "spectrum window calculation latency average is calculated");
    test.requireNear(snapshot.spectrumBinCalculationLatency.maxMs,
                     3.0,
                     0.0001,
                     "spectrum bin calculation latency max is tracked");
    test.requireNear(snapshot.spectrumBinUpdateLatency.averageMs(),
                     5.0,
                     0.0001,
                     "spectrum bin update latency average is calculated");
    test.requireNear(snapshot.spectrumBandSummaryUpdateLatency.maxMs,
                     7.0,
                     0.0001,
                     "spectrum band summary update latency max is tracked");
    test.requireNear(snapshot.spectrumCloseWindowLatency.averageMs(),
                     9.0,
                     0.0001,
                     "spectrum close window latency average is calculated");
    test.requireNear(snapshot.spectrumSnapshotBuildLatency.maxMs,
                     11.0,
                     0.0001,
                     "spectrum snapshot build latency max is tracked");
}

void testSpectrumFastPathCounter(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordSpectrumFastPathUsage(true, true, true);
    metrics.recordSpectrumFastPathUsage(true, false, true);
    metrics.recordSpectrumIncrementalWindowUsage(true, 0);
    metrics.recordSpectrumIncrementalWindowUsage(true, 3);
    metrics.recordSpectrumIncrementalWindowUsage(false, 2);

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.spectrumFastWindowBlocks == 2,
                 "spectrum fast window blocks are counted");
    test.require(snapshot.spectrumFastBinBlocks == 1,
                 "spectrum fast bin blocks are counted");
    test.require(snapshot.spectrumFastBandSummaryBlocks == 2,
                 "spectrum fast band summary blocks are counted");
    test.require(snapshot.spectrumIncrementalWindowBlocks == 2,
                 "spectrum incremental window blocks are counted");
    test.require(snapshot.spectrumIncrementalWindowFallbacks == 5,
                 "spectrum incremental window fallbacks are counted");
}

void testBearingMicroBreakdownLatencyStats(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordBearingSampleLoopLatency(std::chrono::milliseconds{12});
    metrics.recordBearingSampleLoopLatency(std::chrono::milliseconds{18});
    metrics.recordBearingWindowCalculationLatency(std::chrono::milliseconds{2});
    metrics.recordBearingBinCalculationLatency(std::chrono::milliseconds{3});
    metrics.recordBearingCandidateUpdateLatency(std::chrono::milliseconds{7});
    metrics.recordBearingCloseWindowLatency(std::chrono::milliseconds{1});
    metrics.recordBearingSnapshotBuildLatency(std::chrono::milliseconds{4});
    metrics.recordBearingEstimateCalculationLatency(std::chrono::milliseconds{5});

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.bearingSampleLoopLatency.count == 2,
                 "bearing sample loop latency count records");
    test.requireNear(snapshot.bearingSampleLoopLatency.averageMs(),
                     15.0,
                     0.0001,
                     "bearing sample loop latency average is calculated");
    test.requireNear(snapshot.bearingSampleLoopLatency.maxMs,
                     18.0,
                     0.0001,
                     "bearing sample loop latency max is tracked");
    test.requireNear(snapshot.bearingWindowCalculationLatency.averageMs(),
                     2.0,
                     0.0001,
                     "bearing window calculation latency average is calculated");
    test.requireNear(snapshot.bearingBinCalculationLatency.maxMs,
                     3.0,
                     0.0001,
                     "bearing bin calculation latency max is tracked");
    test.requireNear(snapshot.bearingCandidateUpdateLatency.averageMs(),
                     7.0,
                     0.0001,
                     "bearing candidate update latency average is calculated");
    test.requireNear(snapshot.bearingCloseWindowLatency.maxMs,
                     1.0,
                     0.0001,
                     "bearing close window latency max is tracked");
    test.requireNear(snapshot.bearingSnapshotBuildLatency.averageMs(),
                     4.0,
                     0.0001,
                     "bearing snapshot build latency average is calculated");
    test.requireNear(snapshot.bearingEstimateCalculationLatency.maxMs,
                     5.0,
                     0.0001,
                     "bearing estimate calculation latency max is tracked");
}

void testBearingFastCandidateStorageCounter(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordBearingFastCandidateStorageBlock();
    metrics.recordBearingFastCandidateStorageBlock();
    metrics.recordBearingFastCandidateStorageBlock();

    const auto snapshot = snapshotFor(metrics);
    test.require(snapshot.bearingFastCandidateStorageBlocks == 3,
                 "bearing fast candidate storage blocks are counted");
}

void testResetClearsLatencyStats(TestRunner& test)
{
    pipeline::PipelineMetrics metrics;

    metrics.recordProcessBlockLatency(std::chrono::milliseconds{1}, 100);
    metrics.recordSpectrumSnapshotPublishLatency(std::chrono::milliseconds{3});
    metrics.recordSignalParameterFinalizeLatency(std::chrono::milliseconds{4});
    metrics.recordSignalParameterTrustedFixedBandFastPathBlock();
    metrics.recordSpectrumSampleLoopLatency(std::chrono::milliseconds{6});
    metrics.recordSpectrumBandSummaryUpdateLatency(std::chrono::milliseconds{8});
    metrics.recordSpectrumFastPathUsage(true, true, true);
    metrics.recordSpectrumIncrementalWindowUsage(true, 4);
    metrics.recordBearingSampleLoopLatency(std::chrono::milliseconds{2});
    metrics.recordBearingCandidateUpdateLatency(std::chrono::milliseconds{3});
    metrics.recordBearingFastCandidateStorageBlock();
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
    test.require(snapshot.spectrumSampleLoopLatency.count == 0,
                 "reset clears spectrum sample loop latency count");
    test.require(snapshot.spectrumBandSummaryUpdateLatency.count == 0,
                 "reset clears spectrum band summary latency count");
    test.require(snapshot.spectrumFastWindowBlocks == 0,
                 "reset clears spectrum fast window counter");
    test.require(snapshot.spectrumFastBinBlocks == 0,
                 "reset clears spectrum fast bin counter");
    test.require(snapshot.spectrumFastBandSummaryBlocks == 0,
                 "reset clears spectrum fast band summary counter");
    test.require(snapshot.spectrumIncrementalWindowBlocks == 0,
                 "reset clears spectrum incremental window block counter");
    test.require(snapshot.spectrumIncrementalWindowFallbacks == 0,
                 "reset clears spectrum incremental fallback counter");
    test.require(snapshot.bearingSampleLoopLatency.count == 0,
                 "reset clears bearing sample loop latency count");
    test.require(snapshot.bearingCandidateUpdateLatency.count == 0,
                 "reset clears bearing candidate update latency count");
    test.require(snapshot.bearingFastCandidateStorageBlocks == 0,
                 "reset clears bearing fast candidate storage counter");
}

} // namespace

int main()
{
    TestRunner test;

    testLatencyStatsAverageAndMax(test);
    testAverageSamplesPerProcessedBlock(test);
    testSignalParameterMicroBreakdownLatencyStats(test);
    testSignalParameterFastPathCounter(test);
    testSpectrumMicroBreakdownLatencyStats(test);
    testSpectrumFastPathCounter(test);
    testBearingMicroBreakdownLatencyStats(test);
    testBearingFastCandidateStorageCounter(test);
    testResetClearsLatencyStats(test);

    return test.result();
}
