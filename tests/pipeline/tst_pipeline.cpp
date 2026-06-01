#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/interfaces/antenna_azimuth_provider.h"
#include "pipeline/bearing_snapshot.h"
#include "pipeline/bounded_block_queue.h"
#include "pipeline/data_ingest_pipeline.h"
#include "pipeline/pipeline_diagnostics.h"
#include "pipeline/pipeline_metrics.h"
#include "pipeline/processing_engine.h"
#include "pipeline/signal_block_pool.h"
#include "pipeline/snapshot_exchange.h"
#include "pipeline/spectrum_snapshot.h"
#include "pipeline/waterfall_snapshot.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <mutex>
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

class RecordingDiagnosticsSink final : public infrastructure::IDiagnosticsSink
{
public:
    void publish(const infrastructure::DiagnosticEvent& event) override
    {
        events.push_back(event);
    }

    std::vector<infrastructure::DiagnosticEvent> events;
};

class FakeAntennaAzimuthProvider final : public hardware::IAntennaAzimuthProvider
{
public:
    explicit FakeAntennaAzimuthProvider(double azimuthDeg)
        : m_azimuthDeg(azimuthDeg)
    {
    }

    double currentAzimuthDeg() const override { return m_azimuthDeg; }

private:
    double m_azimuthDeg = 0.0;
};

core::BandConfig makeBand(int bandIndex = 0)
{
    constexpr std::int64_t centersHz[] = {
        3'000'000'000LL,
        5'795'000'000LL,
        8'250'000'000LL,
        9'550'000'000LL,
        14'250'000'000LL,
    };
    const auto centerHz = bandIndex >= 0
            && static_cast<std::size_t>(bandIndex) < std::size(centersHz)
        ? centersHz[static_cast<std::size_t>(bandIndex)]
        : 3'000'000'000LL + static_cast<std::int64_t>(bandIndex) * 500'000'000LL;
    const auto created = core::BandConfig::create(
        bandIndex,
        centerHz,
        500'000'000LL);
    return *created.value();
}

core::SignalSample makeSample(const core::BandConfig& band,
                              std::uint64_t sampleIndex,
                              int beamIndex,
                              int amplitude)
{
    const auto created = core::SignalSample::create(
        core::BeamSample{sampleIndex, 0, amplitude, beamIndex},
        band);
    return *created.value();
}

hardware::BcoStreamConfig makeStreamConfig()
{
    hardware::BcoStreamConfig config;
    config.bandConfigs = {makeBand(0), makeBand(1)};
    config.timeBase = core::TimeBase{0, 0, core::DomainConstraints::defaultSamplePeriodNs};
    config.sessionId = 1;
    return config;
}

void testSignalBlockPoolAcquireReleaseExhaustion(TestRunner& test)
{
    pipeline::SignalBlockPool pool(pipeline::SignalBlockPoolConfig{2, 4});

    auto first = pool.acquire();
    auto second = pool.acquire();
    auto exhausted = pool.acquire();
    auto counters = pool.counters();

    test.require(first && second, "pool acquires configured block count");
    test.require(!exhausted, "pool returns empty handle when exhausted");
    test.require(counters.acquired == 2, "pool counts acquired blocks");
    test.require(counters.exhausted == 1, "pool counts exhaustion");
    test.require(counters.inUse == 2, "pool reports in-use blocks");

    first.reset();
    counters = pool.counters();
    test.require(counters.released == 1, "pool release counter increments");
    test.require(counters.available == 1, "pool exposes released block as available");

    auto reused = pool.acquire();
    test.require(static_cast<bool>(reused), "pool reuses released block");
}

void testBoundedBlockQueuePushPopOverflowShutdown(TestRunner& test)
{
    pipeline::SignalBlockPool pool(pipeline::SignalBlockPoolConfig{2, 4});
    pipeline::BoundedBlockQueue queue(1);

    auto first = pool.acquire();
    auto second = pool.acquire();

    const bool pushed = queue.tryPush(std::move(first));
    const bool overflow = queue.tryPush(std::move(second));
    auto metrics = queue.metrics();

    test.require(pushed, "queue accepts first block");
    test.require(!overflow, "queue rejects block over capacity");
    test.require(metrics.depth == 1, "queue depth follows pushed block");
    test.require(metrics.droppedBlocks == 1, "queue counts overflow as dropped block");

    pipeline::SignalBlockHandle popped;
    const bool poppedOk = queue.tryPop(popped);
    metrics = queue.metrics();
    test.require(poppedOk && popped, "queue pops stored block");
    test.require(metrics.depth == 0, "queue depth returns to zero");
    test.require(metrics.poppedBlocks == 1, "queue counts popped block");

    queue.shutdown();
    const auto stoppedPop = queue.pop();
    test.require(!stoppedPop, "shutdown queue returns empty pop");
    test.require(queue.metrics().shutdown, "queue exposes shutdown state");
}

void testProcessingEngineProcessesBlocksAndFlushes(TestRunner& test)
{
    const auto band = makeBand(0);
    pipeline::SignalBlockPool pool(pipeline::SignalBlockPoolConfig{2, 8});
    pipeline::BoundedBlockQueue queue(2);
    pipeline::PipelineMetrics metrics;
    pipeline::PipelineDiagnostics diagnostics(
        pipeline::PipelineDiagnosticsConfig{std::chrono::milliseconds{1}, "ProcessingEngine"});
    pipeline::WaterfallRowQueue waterfallRows;
    pipeline::SnapshotExchange<pipeline::SpectrumSnapshot> spectrumSnapshots;
    pipeline::SnapshotExchange<pipeline::BearingSnapshot> bearingSnapshots;
    pipeline::ProcessingEngine engine(&queue,
                                      &metrics,
                                      &diagnostics,
                                      &waterfallRows,
                                      {},
                                      &spectrumSnapshots,
                                      {},
                                      &bearingSnapshots);

    const auto started = engine.start();
    auto block = pool.acquire();
    pipeline::SignalBlockMetadata metadata{0, 10, 11, std::chrono::steady_clock::now()};
    metadata.antennaAzimuthDeg = 45.0;
    block->reset(metadata);
    const std::vector<core::SignalSample> samples{
        makeSample(band, 10, 0, 40),
        makeSample(band, 10, 1, 90),
        makeSample(band, 11, 1, 45),
    };
    block->assignSamples(samples);
    metrics.recordInputBlock(block->sampleCount(), block->producedAt());
    const bool queued = queue.tryPush(std::move(block));
    const auto flushed = engine.flush(std::chrono::milliseconds{1500});
    const auto summary = engine.lastSummary();
    const auto drainedWaterfallRows = waterfallRows.drain(10);
    const auto spectrumSnapshot = spectrumSnapshots.latest();
    const auto bearingSnapshot = bearingSnapshots.latest();
    const auto metricsSnapshot =
        metrics.snapshot(queue.metrics(), pool.counters(), waterfallRows.metrics());
    engine.stop();

    test.require(started.success, "processing engine starts");
    test.require(queued, "processing engine test queues block");
    test.require(flushed.success, "processing engine flushes queued block");
    test.require(summary.processedBlocks == 1, "processing engine counts processed block");
    test.require(summary.processedSamples == 3, "processing engine counts processed samples");
    test.require(summary.firstSampleIndex == 10 && summary.lastSampleIndex == 11,
                 "processing engine tracks sample index range");
    test.require(!drainedWaterfallRows.empty(),
                 "processing engine queues waterfall rows after flush");
    test.require(spectrumSnapshot && !spectrumSnapshot->bins.empty(),
                 "processing engine publishes spectrum snapshot after flush");
    test.require(bearingSnapshot && !bearingSnapshot->estimates.empty(),
                 "processing engine publishes bearing snapshot after flush");
    test.require(metricsSnapshot.producedWaterfallRows > 0,
                 "pipeline metrics count produced waterfall rows");
    test.require(metricsSnapshot.producedWaterfallSnapshots > 0,
                 "pipeline metrics count produced waterfall row batches");
    test.require(metricsSnapshot.waterfallQueuedRows > 0,
                 "pipeline metrics count queued waterfall rows");
    test.require(metricsSnapshot.waterfallDrainedRows > 0,
                 "pipeline metrics count drained waterfall rows");
    test.require(metricsSnapshot.producedSpectrumSnapshots > 0,
                 "pipeline metrics count produced spectrum snapshots");
    test.require(metricsSnapshot.producedBearingSnapshots > 0,
                 "pipeline metrics count produced bearing snapshots");
    test.require(metricsSnapshot.producedSignalParameterSnapshots > 0,
                 "pipeline metrics count produced signal parameter snapshots");
    test.require(metricsSnapshot.producedBearingEstimates > 0,
                 "pipeline metrics count produced bearing estimates");
    test.require(metricsSnapshot.completeBearingCandidates > 0,
                 "pipeline metrics count complete bearing candidates");
    test.require(metricsSnapshot.processBlockLatency.count == 1,
                 "pipeline metrics count process block latency samples");
    test.require(metricsSnapshot.waterfallAggregationLatency.count == 1,
                 "pipeline metrics count waterfall aggregation latency samples");
    test.require(metricsSnapshot.spectrumAggregationLatency.count == 1,
                 "pipeline metrics count spectrum aggregation latency samples");
    test.require(metricsSnapshot.bearingAggregationLatency.count == 1,
                 "pipeline metrics count bearing aggregation latency samples");
    test.require(metricsSnapshot.signalParameterAggregationLatency.count == 1,
                 "pipeline metrics count signal parameter aggregation latency samples");
    test.require(metricsSnapshot.waterfallRowPublishLatency.count == 1,
                 "pipeline metrics count waterfall row publish latency samples");
    test.require(metricsSnapshot.spectrumSnapshotPublishLatency.count == 1,
                 "pipeline metrics count spectrum snapshot publish latency samples");
    test.require(metricsSnapshot.bearingSnapshotPublishLatency.count == 1,
                 "pipeline metrics count bearing snapshot publish latency samples");
    test.require(metricsSnapshot.signalParameterSnapshotPublishLatency.count == 1,
                 "pipeline metrics count signal parameter snapshot publish latency samples");
    test.require(metricsSnapshot.processedBlockSamplesTotal == 3,
                 "pipeline metrics count samples measured by process block latency");
    test.require(metricsSnapshot.averageSamplesPerProcessedBlock == 3.0,
                 "pipeline metrics expose average samples per processed block");
    test.require(spectrumSnapshot
                     && metricsSnapshot.latestSpectrumSnapshotSequence
                         == spectrumSnapshot->sequenceId,
                 "pipeline metrics expose latest spectrum snapshot sequence");

    bool sawBeam1 = false;
    for (const auto& item : summary.bandBeamSummaries) {
        if (item.bandIndex == 0 && item.beamIndex == 1) {
            sawBeam1 = item.sampleCount == 2 && item.maxAmplitude == 90;
        }
    }
    test.require(sawBeam1, "processing engine aggregates max amplitude per band/beam");
}

void testDiagnosticsAreAggregated(TestRunner& test)
{
    RecordingDiagnosticsSink sink;
    pipeline::PipelineDiagnostics diagnostics(
        pipeline::PipelineDiagnosticsConfig{std::chrono::seconds{30}, "DataIngestPipeline"});

    for (int i = 0; i < 100; ++i) {
        diagnostics.recordDroppedBlock(10);
        diagnostics.recordQueueOverflow();
    }

    const bool firstPublished = diagnostics.publishIfDue(&sink);
    diagnostics.recordDroppedBlock(1);
    const bool suppressed = diagnostics.publishIfDue(&sink);
    const bool forcePublished = diagnostics.publishIfDue(&sink, true);

    test.require(firstPublished, "diagnostics publishes aggregate summary");
    test.require(!suppressed, "diagnostics suppresses repeated summary inside interval");
    test.require(forcePublished, "diagnostics can flush remaining counters");
    test.require(sink.events.size() == 2, "diagnostics does not publish per-event spam");
    test.require(sink.events.front().message.find("droppedBlocks=100") != std::string::npos,
                 "diagnostics summary contains aggregated dropped block count");
}

void testDataPipelineDrainsAllWaterfallRows(TestRunner& test)
{
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{4, 64};
    config.queueCapacity = 4;
    config.diagnosticsPublishInterval = std::chrono::milliseconds{50};
    config.acceptingOnStart = true;
    config.waterfall.renderBinCount = 16;
    config.waterfall.sourceMinHz = 300'000'000;
    config.waterfall.sourceMaxHz = 18'000'000'000LL;
    config.waterfall.rowPeriodNs = 20'000'000;
    config.waterfall.timeBase = core::TimeBase{0, 0, 1'000'000};
    config.waterfallRows = pipeline::WaterfallRowQueueConfig{
        32,
        pipeline::WaterfallOverflowPolicy::DropOldest,
    };
    pipeline::DataIngestPipeline dataPipeline(config);

    const auto band = makeBand(0);
    std::vector<core::SignalSample> samples;
    for (std::uint64_t index = 0; index < 10; ++index) {
        samples.push_back(makeSample(band, index * 20, 0, 40));
    }

    const auto started = dataPipeline.start();
    const auto ingested = dataPipeline.ingestSamples(samples);
    const auto flushed = dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto drained = dataPipeline.drainWaterfallRows(20);
    const auto summary = dataPipeline.lastSummary();
    dataPipeline.stop();

    test.require(started.success, "data pipeline starts for waterfall drain test");
    test.require(ingested.success, "data pipeline ingests multi-row waterfall block");
    test.require(flushed.success, "data pipeline flushes multi-row waterfall block");
    test.require(drained.size() == 10,
                 "drainWaterfallRows returns all produced rows without latest-only loss");
    bool ordered = true;
    for (std::size_t index = 0; index < drained.size(); ++index) {
        ordered = ordered && drained[index].row.firstSampleIndex == index * 20;
    }
    test.require(ordered, "drained waterfall rows preserve production order");
    test.require(summary.metrics.waterfallQueuedRows == 10,
                 "metrics count queued waterfall rows");
    test.require(summary.metrics.waterfallDrainedRows == 10,
                 "metrics count drained waterfall rows");
    test.require(summary.metrics.waterfallDroppedRows == 0,
                 "no waterfall rows drop without overload");
    test.require(summary.metrics.waterfallExpectedRowPeriodMs == 20.0,
                 "pipeline metrics expose expected waterfall row period");
    test.require(summary.metrics.waterfallRowUtcDeltaMinMs == 20.0
                     && summary.metrics.waterfallRowUtcDeltaMaxMs == 20.0,
                 "pipeline metrics expose waterfall UTC row delta range");
    test.require(summary.metrics.waterfallTimebaseMismatchWarnings == 0,
                 "pipeline metrics report no timebase mismatch for contiguous rows");
}

void testWaterfallRowQueueOverflowIsCounted(TestRunner& test)
{
    RecordingDiagnosticsSink diagnostics;
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{4, 64};
    config.queueCapacity = 4;
    config.diagnosticsPublishInterval = std::chrono::milliseconds{0};
    config.acceptingOnStart = true;
    config.waterfall.renderBinCount = 16;
    config.waterfall.sourceMinHz = 300'000'000;
    config.waterfall.sourceMaxHz = 18'000'000'000LL;
    config.waterfall.rowPeriodNs = 20'000'000;
    config.waterfall.timeBase = core::TimeBase{0, 0, 1'000'000};
    config.waterfallRows = pipeline::WaterfallRowQueueConfig{
        3,
        pipeline::WaterfallOverflowPolicy::DropOldest,
    };
    pipeline::DataIngestPipeline dataPipeline(config, &diagnostics);

    const auto band = makeBand(0);
    std::vector<core::SignalSample> samples;
    for (std::uint64_t index = 0; index < 5; ++index) {
        samples.push_back(makeSample(band, index * 20, 0, 40));
    }

    dataPipeline.start();
    dataPipeline.ingestSamples(samples);
    dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto drained = dataPipeline.drainWaterfallRows(10);
    const auto metrics = dataPipeline.metricsSnapshot();
    dataPipeline.stop();

    test.require(drained.size() == 3, "overflow leaves bounded waterfall rows queued");
    test.require(drained.front().row.firstSampleIndex == 40
                     && drained.back().row.firstSampleIndex == 80,
                 "DropOldest waterfall delivery keeps newest rows under overload");
    test.require(metrics.waterfallQueuedRows == 5,
                 "metrics count attempted queued waterfall rows");
    test.require(metrics.waterfallDroppedRows == 2,
                 "metrics count dropped waterfall rows");
    test.require(!diagnostics.events.empty()
                     && diagnostics.events.front().message.find("Waterfall row queue overflow")
                         != std::string::npos,
                 "diagnostics publish rate-limited waterfall overflow summary");
}

void testWaterfallRowTimebaseMismatchIsCounted(TestRunner& test)
{
    RecordingDiagnosticsSink diagnostics;
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{4, 64};
    config.queueCapacity = 4;
    config.diagnosticsPublishInterval = std::chrono::milliseconds{0};
    config.acceptingOnStart = true;
    config.waterfall.renderBinCount = 16;
    config.waterfall.sourceMinHz = 300'000'000;
    config.waterfall.sourceMaxHz = 18'000'000'000LL;
    config.waterfall.rowPeriodNs = 20'000'000;
    config.waterfall.timeBase = core::TimeBase{1'000'000'000, 0, 1'000'000};
    config.waterfallRows = pipeline::WaterfallRowQueueConfig{
        32,
        pipeline::WaterfallOverflowPolicy::DropOldest,
    };
    pipeline::DataIngestPipeline dataPipeline(config, &diagnostics);

    const auto band = makeBand(0);
    const std::vector<core::SignalSample> samples{
        makeSample(band, 0, 0, 40),
        makeSample(band, 40, 0, 40),
        makeSample(band, 60, 0, 40),
    };

    dataPipeline.start();
    dataPipeline.ingestSamples(samples);
    dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto drained = dataPipeline.drainWaterfallRows(10);
    const auto metrics = dataPipeline.metricsSnapshot();
    dataPipeline.stop();

    test.require(drained.size() == 3,
                 "timebase mismatch test produces sparse waterfall rows");
    test.require(metrics.waterfallExpectedRowPeriodMs == 20.0,
                 "timebase mismatch metrics keep expected row period");
    test.require(metrics.waterfallRowUtcDeltaMinMs == 20.0
                     && metrics.waterfallRowUtcDeltaMaxMs == 40.0,
                 "timebase mismatch metrics expose sparse UTC delta range");
    test.require(metrics.waterfallTimebaseMismatchWarnings == 1,
                 "timebase mismatch metrics count row delta warning");
    test.require(!diagnostics.events.empty()
                     && diagnostics.events.front().message.find(
                            "waterfall row time delta mismatch")
                         != std::string::npos,
                 "timebase mismatch diagnostic is aggregated");
}

void testHighLoadSimulatorConnectsToDataIngestPipeline(TestRunner& test)
{
    RecordingDiagnosticsSink diagnostics;
    pipeline::DataIngestPipeline dataPipeline(
        pipeline::DataIngestPipelineConfig{
            pipeline::SignalBlockPoolConfig{8, 20'000},
            4,
            std::chrono::milliseconds{50},
            true,
        },
        &diagnostics);

    hardware::SimulatorBcoLoadConfig loadConfig;
    loadConfig.profile = hardware::SimulatorLoadProfile::RealBcoEquivalent;
    FakeAntennaAzimuthProvider antenna(45.0);
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig, &diagnostics, &antenna);
    const auto configured = source.configure(makeStreamConfig());
    const auto pipelineStarted = dataPipeline.start();

    std::mutex mutex;
    std::condition_variable condition;
    int receivedBlocks = 0;
    const auto sourceStarted = source.start([&](auto block) {
        if (!block) {
            return;
        }

        pipeline::SignalBlockMetadata metadata;
        metadata.firstSampleIndex = block->stats.firstSampleIndex;
        metadata.lastSampleIndex = block->stats.lastSampleIndex;
        metadata.producedAt = block->stats.producedAt;
        metadata.antennaAzimuthDeg = block->stats.antennaAzimuthDeg;
        dataPipeline.ingestSamples(block->samples, metadata);

        {
            std::lock_guard lock(mutex);
            ++receivedBlocks;
        }
        condition.notify_one();
    });

    std::unique_lock lock(mutex);
    const bool arrived = condition.wait_for(lock, std::chrono::milliseconds{750}, [&] {
        return receivedBlocks >= 3;
    });
    lock.unlock();

    source.stop();
    const auto flushed = dataPipeline.flushProcessing(std::chrono::milliseconds{1500});
    const auto waterfallRows = dataPipeline.drainWaterfallRows(10'000);
    const auto summary = dataPipeline.lastSummary();
    const auto spectrumSnapshot = dataPipeline.latestSpectrumSnapshot();
    const auto bearingSnapshot = dataPipeline.latestBearingSnapshot();
    dataPipeline.stop();

    test.require(configured.success, "high-load simulator accepts stream config");
    test.require(pipelineStarted.success, "data ingest pipeline starts");
    test.require(sourceStarted.success, "high-load simulator starts");
    test.require(arrived, "high-load simulator emits bounded smoke blocks");
    test.require(flushed.success, "data ingest pipeline flushes simulator blocks");
    test.require(summary.processedBlocks > 0, "data ingest pipeline processes simulator blocks");
    test.require(summary.metrics.processedSamples > 0,
                 "pipeline metrics count processed simulator samples");
    test.require(!waterfallRows.empty(),
                 "data ingest pipeline publishes queued waterfall rows");
    test.require(spectrumSnapshot && !spectrumSnapshot->bins.empty(),
                 "data ingest pipeline publishes spectrum snapshot");
    test.require(bearingSnapshot && !bearingSnapshot->estimates.empty(),
                 "data ingest pipeline publishes bearing snapshot");
    test.require(summary.metrics.producedWaterfallSnapshots > 0,
                 "data ingest metrics count produced waterfall row batches");
    test.require(summary.metrics.waterfallQueuedRows > 0,
                 "data ingest metrics count queued waterfall rows");
    test.require(summary.metrics.waterfallDrainedRows > 0,
                 "data ingest metrics count drained waterfall rows");
    test.require(summary.metrics.producedSpectrumSnapshots > 0,
                 "data ingest metrics count produced spectrum snapshots");
    test.require(summary.metrics.producedBearingSnapshots > 0,
                 "data ingest metrics count produced bearing snapshots");
    test.require(summary.metrics.producedBearingEstimates > 0,
                 "data ingest metrics count produced bearing estimates");
}

} // namespace

int main()
{
    TestRunner test;

    testSignalBlockPoolAcquireReleaseExhaustion(test);
    testBoundedBlockQueuePushPopOverflowShutdown(test);
    testProcessingEngineProcessesBlocksAndFlushes(test);
    testDiagnosticsAreAggregated(test);
    testDataPipelineDrainsAllWaterfallRows(test);
    testWaterfallRowQueueOverflowIsCounted(test);
    testWaterfallRowTimebaseMismatchIsCounted(test);
    testHighLoadSimulatorConnectsToDataIngestPipeline(test);

    return test.result();
}
