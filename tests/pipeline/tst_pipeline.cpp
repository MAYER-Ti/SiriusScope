#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
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

core::BandConfig makeBand(int bandIndex = 0)
{
    const auto created = core::BandConfig::create(
        bandIndex,
        3'000'000'000LL + static_cast<std::int64_t>(bandIndex) * 100'000'000LL,
        100'000'000LL);
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
    pipeline::SnapshotExchange<pipeline::WaterfallSnapshot> waterfallSnapshots;
    pipeline::SnapshotExchange<pipeline::SpectrumSnapshot> spectrumSnapshots;
    pipeline::ProcessingEngine engine(&queue,
                                      &metrics,
                                      &diagnostics,
                                      &waterfallSnapshots,
                                      {},
                                      &spectrumSnapshots);

    const auto started = engine.start();
    auto block = pool.acquire();
    block->reset(pipeline::SignalBlockMetadata{0, 10, 10, std::chrono::steady_clock::now()});
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
    const auto waterfallSnapshot = waterfallSnapshots.latest();
    const auto spectrumSnapshot = spectrumSnapshots.latest();
    const auto metricsSnapshot = metrics.snapshot(queue.metrics(), pool.counters());
    engine.stop();

    test.require(started.success, "processing engine starts");
    test.require(queued, "processing engine test queues block");
    test.require(flushed.success, "processing engine flushes queued block");
    test.require(summary.processedBlocks == 1, "processing engine counts processed block");
    test.require(summary.processedSamples == 3, "processing engine counts processed samples");
    test.require(summary.firstSampleIndex == 10 && summary.lastSampleIndex == 11,
                 "processing engine tracks sample index range");
    test.require(waterfallSnapshot && !waterfallSnapshot->rows.empty(),
                 "processing engine publishes waterfall snapshot after flush");
    test.require(spectrumSnapshot && !spectrumSnapshot->bins.empty(),
                 "processing engine publishes spectrum snapshot after flush");
    test.require(metricsSnapshot.producedWaterfallRows > 0,
                 "pipeline metrics count produced waterfall rows");
    test.require(metricsSnapshot.producedWaterfallSnapshots > 0,
                 "pipeline metrics count produced waterfall snapshots");
    test.require(metricsSnapshot.producedSpectrumSnapshots > 0,
                 "pipeline metrics count produced spectrum snapshots");
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
    hardware::HighLoadSimulatorBcoStreamSource source(loadConfig, &diagnostics);
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
    const auto summary = dataPipeline.lastSummary();
    const auto waterfallSnapshot = dataPipeline.latestWaterfallSnapshot();
    const auto spectrumSnapshot = dataPipeline.latestSpectrumSnapshot();
    dataPipeline.stop();

    test.require(configured.success, "high-load simulator accepts stream config");
    test.require(pipelineStarted.success, "data ingest pipeline starts");
    test.require(sourceStarted.success, "high-load simulator starts");
    test.require(arrived, "high-load simulator emits bounded smoke blocks");
    test.require(flushed.success, "data ingest pipeline flushes simulator blocks");
    test.require(summary.processedBlocks > 0, "data ingest pipeline processes simulator blocks");
    test.require(summary.metrics.processedSamples > 0,
                 "pipeline metrics count processed simulator samples");
    test.require(waterfallSnapshot && !waterfallSnapshot->rows.empty(),
                 "data ingest pipeline publishes waterfall snapshot");
    test.require(spectrumSnapshot && !spectrumSnapshot->bins.empty(),
                 "data ingest pipeline publishes spectrum snapshot");
    test.require(summary.metrics.producedWaterfallSnapshots > 0,
                 "data ingest metrics count produced waterfall snapshots");
    test.require(summary.metrics.producedSpectrumSnapshots > 0,
                 "data ingest metrics count produced spectrum snapshots");
}

} // namespace

int main()
{
    TestRunner test;

    testSignalBlockPoolAcquireReleaseExhaustion(test);
    testBoundedBlockQueuePushPopOverflowShutdown(test);
    testProcessingEngineProcessesBlocksAndFlushes(test);
    testDiagnosticsAreAggregated(test);
    testHighLoadSimulatorConnectsToDataIngestPipeline(test);

    return test.result();
}
