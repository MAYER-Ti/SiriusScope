#include "core/domain_constraints.h"
#include "core/domain_models.h"
#include "hardware/interfaces/antenna_azimuth_provider.h"
#include "hardware/interfaces/bco_stream_source.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/simulator/simulated_bco_payload_accounting.h"
#include "pipeline/data_ingest_pipeline.h"
#include "pipeline/pipeline_metrics.h"
#include "pipeline/waterfall_row_queue.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace siriusscope;

constexpr std::int64_t kSourceMinHz = core::DomainConstraints::minSystemFrequencyHz;
constexpr std::int64_t kSourceMaxHz = core::DomainConstraints::maxSystemFrequencyHz;
constexpr int kRenderBinCount = 512;
constexpr std::uint64_t kRowPeriodNs = 20'000'000;
constexpr std::uint64_t kTargetRawBytesPerSecond = 90'000'000;
constexpr std::uint64_t kExpectedTargetRawBytesPerSecond = 89'990'400;

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

class FixedAntennaAzimuthProvider final : public hardware::IAntennaAzimuthProvider
{
public:
    explicit FixedAntennaAzimuthProvider(double azimuthDeg)
        : m_azimuthDeg(azimuthDeg)
    {
    }

    double currentAzimuthDeg() const override
    {
        return m_azimuthDeg;
    }

private:
    double m_azimuthDeg = 0.0;
};

struct AuditResult
{
    std::string profileName;
    std::chrono::seconds duration{0};
    hardware::BcoSourceMetrics source;
    pipeline::PipelineMetricsSnapshot pipeline;
    pipeline::WaterfallRowQueueMetrics waterfallRows;
    std::uint64_t rejectedBlocks = 0;
    std::uint64_t rejectedSamples = 0;
    std::size_t drainedWaterfallRows = 0;
    bool sourceConfigured = false;
    bool pipelineStarted = false;
    bool sourceStarted = false;
    bool sourceStopped = false;
    bool flushed = false;
    bool hasSpectrumSnapshot = false;
    bool hasBearingSnapshot = false;
    bool hasSignalParameterSnapshot = false;
};

std::int64_t nowUtcNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

hardware::ThroughputTarget targetRaw90Mbps()
{
    hardware::ThroughputTarget target;
    target.targetBytesPerSecond = kTargetRawBytesPerSecond;
    target.batchPeriod = std::chrono::milliseconds{10};
    target.mode = hardware::PayloadAccountingMode::RawBcoBytes;
    target.packetModel.packetHeaderBytes = 32;
    target.packetModel.sampleRecordBytes = 16;
    target.packetModel.packetFooterBytes = 0;
    target.packetModel.samplesPerPacket = 256;
    target.packetModel.alignmentBytes = 0;
    return target;
}

std::size_t samplesPerSecondFor(hardware::SimulatorLoadProfile profile)
{
    switch (profile) {
    case hardware::SimulatorLoadProfile::UiDemo:
        return 1'280;
    case hardware::SimulatorLoadProfile::MediumLoad:
        return 250'000;
    case hardware::SimulatorLoadProfile::RealBcoEquivalent:
        return 1'000'000;
    case hardware::SimulatorLoadProfile::Stress150Percent:
        return 1'500'000;
    case hardware::SimulatorLoadProfile::TargetRawThroughput90MBps: {
        const auto target = targetRaw90Mbps();
        const auto samplesPerBatch = hardware::samplesPerBatchForTarget(target);
        const auto batchPeriodMs = std::max<std::int64_t>(1, target.batchPeriod.count());
        return std::max<std::size_t>(
            1,
            samplesPerBatch * 1000ULL / static_cast<std::uint64_t>(batchPeriodMs));
    }
    }

    return 1;
}

std::uint64_t samplePeriodNsFor(hardware::SimulatorLoadProfile profile)
{
    const auto samplesPerSecond = samplesPerSecondFor(profile);
    return std::max<std::uint64_t>(
        1,
        1'000'000'000ULL / static_cast<std::uint64_t>(samplesPerSecond));
}

std::string profileName(hardware::SimulatorLoadProfile profile)
{
    switch (profile) {
    case hardware::SimulatorLoadProfile::UiDemo:
        return "UiDemo";
    case hardware::SimulatorLoadProfile::MediumLoad:
        return "MediumLoad";
    case hardware::SimulatorLoadProfile::RealBcoEquivalent:
        return "RealBcoEquivalent";
    case hardware::SimulatorLoadProfile::Stress150Percent:
        return "Stress150Percent";
    case hardware::SimulatorLoadProfile::TargetRawThroughput90MBps:
        return "TargetRawThroughput90MBps";
    }

    return "Unknown";
}

core::BandConfig makeBand(int bandIndex)
{
    constexpr std::int64_t centersHz[] = {
        3'000'000'000LL,
        5'795'000'000LL,
        8'250'000'000LL,
        9'550'000'000LL,
        14'250'000'000LL,
    };

    const auto created = core::BandConfig::create(
        bandIndex,
        centersHz[static_cast<std::size_t>(bandIndex)],
        core::DomainConstraints::maxBandWidthHz);
    return *created.value();
}

core::TimeBase makeTimeBase(hardware::SimulatorLoadProfile profile)
{
    const auto created = core::TimeBase::create(nowUtcNs(),
                                               0,
                                               samplePeriodNsFor(profile));
    return *created.value();
}

hardware::BcoStreamConfig makeStreamConfig(hardware::SimulatorLoadProfile profile)
{
    hardware::BcoStreamConfig config;
    config.bandConfigs = {
        makeBand(0),
        makeBand(1),
        makeBand(2),
        makeBand(3),
        makeBand(4),
    };
    config.timeBase = makeTimeBase(profile);
    config.sessionId = 1;
    return config;
}

pipeline::DataIngestPipelineConfig makePipelineConfig(
    const core::TimeBase& timeBase,
    hardware::SimulatorLoadProfile profile)
{
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{256, 20'000};
    config.queueCapacity = 256;
    if (profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps) {
        config.blockPool = pipeline::SignalBlockPoolConfig{4, 80'000};
        config.queueCapacity = 2;
    }
    config.diagnosticsPublishInterval = std::chrono::milliseconds{1000};
    config.acceptingOnStart = true;

    config.waterfall.renderBinCount = kRenderBinCount;
    config.waterfall.sourceMinHz = kSourceMinHz;
    config.waterfall.sourceMaxHz = kSourceMaxHz;
    config.waterfall.rowPeriodNs = kRowPeriodNs;
    config.waterfall.timeBase = timeBase;
    config.waterfallRows = pipeline::WaterfallRowQueueConfig{
        4096,
        pipeline::WaterfallOverflowPolicy::DropOldest,
    };

    config.spectrum.renderBinCount = kRenderBinCount;
    config.spectrum.sourceMinHz = kSourceMinHz;
    config.spectrum.sourceMaxHz = kSourceMaxHz;
    config.spectrum.snapshotPeriodNs = kRowPeriodNs;
    config.spectrum.timeBase = timeBase;

    config.bearing.frequencyBinCount = kRenderBinCount;
    config.bearing.sourceMinHz = kSourceMinHz;
    config.bearing.sourceMaxHz = kSourceMaxHz;
    config.bearing.windowPeriodNs = kRowPeriodNs;
    config.bearing.fallbackAntennaAzimuthDeg = 45.0;
    config.bearing.timeBase = timeBase;

    config.signalParameters.estimatorConfig.samplePeriodNs = timeBase.samplePeriodNs;
    return config;
}

hardware::SimulatorBcoLoadConfig makeLoadConfig(hardware::SimulatorLoadProfile profile)
{
    hardware::SimulatorBcoLoadConfig config;
    config.profile = profile;
    config.batchPeriod = std::chrono::milliseconds{10};
    config.deterministic = true;
    config.minVisibleAmplitude = 1;
    return config;
}

AuditResult runAudit(std::chrono::seconds duration,
                     hardware::SimulatorLoadProfile profile,
                     std::chrono::milliseconds flushTimeout = std::chrono::milliseconds{5000},
                     std::optional<std::uint64_t> maxPipelineIngestedBlocks = std::nullopt)
{
    AuditResult result;
    result.profileName = profileName(profile);
    result.duration = duration;

    const auto streamConfig = makeStreamConfig(profile);
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(streamConfig.timeBase,
                                                                 profile));
    FixedAntennaAzimuthProvider antenna(45.0);
    hardware::HighLoadSimulatorBcoStreamSource source(makeLoadConfig(profile),
                                                      nullptr,
                                                      &antenna);

    const auto configured = source.configure(streamConfig);
    result.sourceConfigured = configured.success;
    if (!configured) {
        return result;
    }

    const auto pipelineStarted = dataPipeline.start();
    result.pipelineStarted = pipelineStarted.success;
    if (!pipelineStarted) {
        return result;
    }

    std::atomic_uint64_t rejectedBlocks{0};
    std::atomic_uint64_t rejectedSamples{0};
    std::atomic_uint64_t pipelineIngestedBlocks{0};

    const auto sourceStarted = source.start([&](hardware::IBcoStreamSource::SampleBlockPtr block) {
        if (!block) {
            return;
        }
        if (maxPipelineIngestedBlocks
            && pipelineIngestedBlocks.load(std::memory_order_relaxed)
                >= *maxPipelineIngestedBlocks) {
            return;
        }

        pipeline::SignalBlockMetadata metadata;
        metadata.firstSampleIndex = block->stats.firstSampleIndex;
        metadata.lastSampleIndex = block->stats.lastSampleIndex;
        metadata.producedAt = block->stats.producedAt;
        metadata.antennaAzimuthDeg = block->stats.antennaAzimuthDeg;

        const auto ingested = dataPipeline.ingestSamples(block->samples, metadata);
        if (!ingested) {
            rejectedBlocks.fetch_add(1, std::memory_order_relaxed);
            rejectedSamples.fetch_add(static_cast<std::uint64_t>(block->samples.size()),
                                      std::memory_order_relaxed);
        } else {
            pipelineIngestedBlocks.fetch_add(1, std::memory_order_relaxed);
        }
    });
    result.sourceStarted = sourceStarted.success;

    if (sourceStarted) {
        std::this_thread::sleep_for(duration);
    }

    const auto sourceStopped = source.stop();
    result.sourceStopped = sourceStopped.success;

    const auto flushed = dataPipeline.flushProcessing(flushTimeout);
    result.flushed = flushed.success;

    const auto drainedRows = dataPipeline.drainWaterfallRows(1'000'000);
    result.drainedWaterfallRows = drainedRows.size();
    result.source = source.metrics();
    result.pipeline = dataPipeline.metricsSnapshot();
    result.waterfallRows = dataPipeline.waterfallRowQueueMetrics();
    result.rejectedBlocks = rejectedBlocks.load(std::memory_order_relaxed);
    result.rejectedSamples = rejectedSamples.load(std::memory_order_relaxed);
    result.hasSpectrumSnapshot = dataPipeline.latestSpectrumSnapshot() != nullptr;
    result.hasBearingSnapshot = dataPipeline.latestBearingSnapshot() != nullptr;
    result.hasSignalParameterSnapshot =
        dataPipeline.latestSignalParameterSnapshot() != nullptr;

    dataPipeline.stop();
    return result;
}

void printAuditSummary(const AuditResult& result)
{
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "High-load data plane audit:\n"
              << "  profile = " << result.profileName << '\n'
              << "  durationSec = " << result.duration.count() << '\n'
              << "  source producedSamples = " << result.source.producedSamples << '\n'
              << "  source producedBatches = " << result.source.producedBatches << '\n'
              << "  source producedSamplesPerSecond = "
              << result.source.producedSamplesPerSecond << '\n'
              << "  source equivalentMBps = "
              << result.source.equivalentMegabytesPerSecond << '\n'
              << "  source targetBytesPerSecond = "
              << result.source.targetBytesPerSecond << '\n'
              << "  source producedRawBytes = "
              << result.source.producedRawBytes << '\n'
              << "  source producedRawBytesPerSecond = "
              << result.source.producedRawBytesPerSecond << '\n'
              << "  source producedParsedSamplesPerSecond = "
              << result.source.producedParsedSamplesPerSecond << '\n'
              << "  source scheduleLagMaxMs = "
              << result.source.scheduleLagMax.count() << '\n'
              << "  source missedBatchDeadlines = "
              << result.source.missedBatchDeadlines << '\n'
              << "  source simulatorBackpressureEvents = "
              << result.source.simulatorBackpressureEvents << '\n'
              << "  source maxCallbackDurationMs = "
              << result.source.maxCallbackDuration.count() << '\n'
              << "  pipeline inputSamples = " << result.pipeline.inputSamples << '\n'
              << "  pipeline processedSamples = " << result.pipeline.processedSamples
              << '\n'
              << "  pipeline droppedSamples = " << result.pipeline.droppedSamples << '\n'
              << "  pipeline droppedBlocks = " << result.pipeline.droppedBlocks << '\n'
              << "  pipeline inputMBps = "
              << result.pipeline.inputMegabytesPerSecond << '\n'
              << "  pipeline processedMBps = "
              << result.pipeline.processedMegabytesPerSecond << '\n'
              << "  queueDepth = " << result.pipeline.queueDepth << '\n'
              << "  queueDroppedBlocks = " << result.pipeline.queueDroppedBlocks << '\n'
              << "  blockPoolExhausted = " << result.pipeline.blockPoolExhausted << '\n'
              << "  blockPoolUsage = " << result.pipeline.blockPoolUsage << '\n'
              << "  maxBlockAgeMs = " << result.pipeline.maxBlockAgeMs << '\n'
              << "  processingLatencyMaxMs = "
              << result.pipeline.processingLatencyMaxMs << '\n'
              << "  producedWaterfallRows = "
              << result.pipeline.producedWaterfallRows << '\n'
              << "  drainedWaterfallRows = " << result.drainedWaterfallRows << '\n'
              << "  waterfallDroppedRows = "
              << result.pipeline.waterfallDroppedRows << '\n'
              << "  rejectedBlocks = " << result.rejectedBlocks << '\n'
              << "  rejectedSamples = " << result.rejectedSamples << '\n';
}

void assertAuditSucceeded(TestRunner& test, const AuditResult& result)
{
    test.require(result.sourceConfigured, "source accepts stream config");
    test.require(result.pipelineStarted, "data ingest pipeline starts");
    test.require(result.sourceStarted, "high-load source starts");
    test.require(result.sourceStopped, "high-load source stops");
    test.require(result.flushed, "data ingest pipeline flushes");

    test.require(result.source.producedSamples > 0, "source produces samples");
    test.require(result.source.producedBatches > 0, "source produces batches");
    test.require(result.source.producedSamplesPerSecond > 0.0,
                 "source reports positive sample throughput");
    test.require(result.source.equivalentMegabytesPerSecond > 0.0,
                 "source reports positive equivalent MBps");
    test.require(result.source.maxCallbackDuration.count() < 5000,
                 "source callback duration stays bounded");

    test.require(result.pipeline.inputSamples > 0, "pipeline receives samples");
    test.require(result.pipeline.processedSamples > 0, "pipeline processes samples");
    test.require(result.pipeline.inputMegabytesPerSecond > 0.0,
                 "pipeline reports positive input MBps");
    test.require(result.pipeline.processedMegabytesPerSecond > 0.0,
                 "pipeline reports positive processed MBps");
    test.require(result.pipeline.processedSamples == result.pipeline.inputSamples,
                 "pipeline processes every accepted sample");

    test.require(result.rejectedBlocks == 0, "callback does not reject blocks");
    test.require(result.rejectedSamples == 0, "callback does not reject samples");
    test.require(result.pipeline.droppedSamples == 0, "pipeline reports no dropped samples");
    test.require(result.pipeline.droppedBlocks == 0, "pipeline reports no dropped blocks");
    test.require(result.pipeline.queueDroppedBlocks == 0,
                 "bounded queue reports no dropped blocks");
    test.require(result.pipeline.blockPoolExhausted == 0,
                 "block pool reports no exhaustion");
    test.require(result.pipeline.queueDepth == 0, "queue drains after flush");

    test.require(result.pipeline.maxBlockAgeMs < 5000.0,
                 "max block age stays bounded");
    test.require(result.pipeline.processingLatencyMaxMs < 5000.0,
                 "processing latency stays bounded");
    test.require(result.pipeline.producedWaterfallRows > 0,
                 "pipeline produces waterfall rows");
    test.require(result.drainedWaterfallRows > 0, "audit drains waterfall rows");
    test.require(result.pipeline.waterfallDroppedRows == 0,
                 "waterfall row queue reports no dropped rows");

    test.require(result.hasSpectrumSnapshot, "pipeline publishes spectrum snapshot");
    test.require(result.hasBearingSnapshot, "pipeline publishes bearing snapshot");
    test.require(result.hasSignalParameterSnapshot,
                 "pipeline publishes signal parameter snapshot");
}

bool stressTestsEnabled()
{
    const char* value = std::getenv("SIRIUSSCOPE_RUN_STRESS_TESTS");
    if (!value) {
        return false;
    }

    const std::string text(value);
    return !text.empty() && text != "0";
}

void highLoadDataPlaneSmoke(TestRunner& test)
{
    const auto result = runAudit(std::chrono::seconds{3},
                                 hardware::SimulatorLoadProfile::MediumLoad);
    printAuditSummary(result);
    assertAuditSucceeded(test, result);
}

void highLoadDataPlaneStress(TestRunner& test)
{
    if (!stressTestsEnabled()) {
        return;
    }

    // 30-minute stress audit can be run manually by increasing this duration locally.
    const auto result = runAudit(std::chrono::seconds{60},
                                 hardware::SimulatorLoadProfile::RealBcoEquivalent);
    printAuditSummary(result);
    assertAuditSucceeded(test, result);
}

void targetRaw90mbpsAccountingSmoke(TestRunner& test)
{
    const auto target = targetRaw90Mbps();
    const auto targetSamplesPerBatch = hardware::samplesPerBatchForTarget(target);
    const auto targetRawBytesPerBatch =
        hardware::rawBytesForSamples(targetSamplesPerBatch, target.packetModel);

    test.require(hardware::packetsPerBatchForTarget(target) == 218,
                 "target raw source uses packet-aligned batch packet count");
    test.require(targetSamplesPerBatch == 55'808,
                 "target raw source uses packet-aligned batch sample count");
    test.require(targetRawBytesPerBatch == 899'904,
                 "target raw source accounts packet-aligned raw bytes per batch");
    test.require(targetRawBytesPerBatch * 100ULL == kExpectedTargetRawBytesPerSecond,
                 "target raw source expected throughput is closest packet-aligned value below target");

    // This target raw smoke validates source accounting only; full-pipeline
    // 90 MB/s sustain belongs to later P1 stages.
    const auto result = runAudit(std::chrono::seconds{3},
                                 hardware::SimulatorLoadProfile::
                                     TargetRawThroughput90MBps,
                                 std::chrono::milliseconds{250},
                                 std::uint64_t{1});
    printAuditSummary(result);

    test.require(result.sourceConfigured, "target raw source accepts stream config");
    test.require(result.pipelineStarted, "target raw data ingest pipeline starts");
    test.require(result.sourceStarted, "target raw high-load source starts");
    test.require(result.sourceStopped, "target raw high-load source stops");
    test.require(result.source.targetBytesPerSecond == kTargetRawBytesPerSecond,
                 "target raw source reports configured raw byte target");
    test.require(result.source.producedRawBytes > 0,
                 "target raw source reports produced raw bytes");
    test.require(result.source.producedRawBytesPerSecond > 0.0,
                 "target raw source reports positive raw throughput");
    test.require(result.source.producedParsedSamplesPerSecond > 0.0,
                 "target raw source reports positive parsed sample throughput");

    const double relativeError =
        std::abs(result.source.producedRawBytesPerSecond
                 - static_cast<double>(kTargetRawBytesPerSecond))
        / static_cast<double>(kTargetRawBytesPerSecond);
    test.require(relativeError <= 0.15,
                 "target raw source stays within 15 percent of 90 MBps smoke target");
}

} // namespace

int main()
{
    TestRunner test;

    highLoadDataPlaneSmoke(test);
    targetRaw90mbpsAccountingSmoke(test);
    highLoadDataPlaneStress(test);

    return test.result();
}
