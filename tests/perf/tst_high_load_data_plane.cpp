#include "core/domain_constraints.h"
#include "core/domain_models.h"
#include "hardware/interfaces/antenna_azimuth_provider.h"
#include "hardware/interfaces/bco_stream_source.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/simulator/simulated_bco_payload_accounting.h"
#include "pipeline/data_ingest_pipeline.h"
#include "pipeline/pipeline_metrics.h"
#include "pipeline/source_to_pipeline_bridge.h"
#include "pipeline/waterfall_row_queue.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
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
    pipeline::SourceToPipelineBridgeMetrics bridge;
    pipeline::PipelineMetricsSnapshot pipeline;
    pipeline::WaterfallRowQueueMetrics waterfallRows;
    std::uint64_t rejectedBlocks = 0;
    std::uint64_t rejectedSamples = 0;
    std::size_t drainedWaterfallRows = 0;
    bool sourceConfigured = false;
    bool pipelineStarted = false;
    bool bridgeStarted = false;
    bool sourceStarted = false;
    bool sourceStopped = false;
    bool bridgeFlushed = false;
    bool flushed = false;
    bool hasSpectrumSnapshot = false;
    bool hasBearingSnapshot = false;
    bool hasSignalParameterSnapshot = false;
};

struct LatencyStage
{
    const char* name = "";
    pipeline::LatencyStats stats;
};

enum class AuditPipelineSizing
{
    Default,
    FullTargetRawSustain,
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

double ratioOrZero(double numerator, double denominator)
{
    if (denominator <= 0.0) {
        return 0.0;
    }

    return numerator / denominator;
}

bool envFlagEnabled(const char* name)
{
    const char* value = std::getenv(name);
    if (!value) {
        return false;
    }

    const std::string text(value);
    return !text.empty() && text != "0";
}

bool targetRawPipelineTestEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST");
}

bool strictTargetRawPipelineSustainRequired()
{
    return envFlagEnabled("SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS");
}

std::vector<LatencyStage> stageLatencies(const pipeline::PipelineMetricsSnapshot& metrics)
{
    return {
        {"WaterfallAggregator", metrics.waterfallAggregationLatency},
        {"SpectrumAggregator", metrics.spectrumAggregationLatency},
        {"BearingAggregator", metrics.bearingAggregationLatency},
        {"SignalParameterAggregator", metrics.signalParameterAggregationLatency},
        {"WaterfallRowPublish", metrics.waterfallRowPublishLatency},
        {"SpectrumSnapshotPublish", metrics.spectrumSnapshotPublishLatency},
        {"BearingSnapshotPublish", metrics.bearingSnapshotPublishLatency},
        {"SignalParameterSnapshotPublish", metrics.signalParameterSnapshotPublishLatency},
    };
}

std::vector<LatencyStage> signalParameterInternalLatencies(
    const pipeline::PipelineMetricsSnapshot& metrics)
{
    return {
        {"ingest", metrics.signalParameterIngestLatency},
        {"snapshotDecision", metrics.signalParameterSnapshotDecisionLatency},
        {"finalize", metrics.signalParameterFinalizeLatency},
        {"snapshotBuild", metrics.signalParameterSnapshotBuildLatency},
    };
}

std::vector<LatencyStage> spectrumInternalLatencies(
    const pipeline::PipelineMetricsSnapshot& metrics)
{
    return {
        {"sampleLoop", metrics.spectrumSampleLoopLatency},
        {"windowCalculation", metrics.spectrumWindowCalculationLatency},
        {"binCalculation", metrics.spectrumBinCalculationLatency},
        {"binUpdate", metrics.spectrumBinUpdateLatency},
        {"bandSummaryUpdate", metrics.spectrumBandSummaryUpdateLatency},
        {"closeWindow", metrics.spectrumCloseWindowLatency},
        {"snapshotBuild", metrics.spectrumSnapshotBuildLatency},
    };
}

std::vector<LatencyStage> bearingInternalLatencies(
    const pipeline::PipelineMetricsSnapshot& metrics)
{
    return {
        {"windowCalculation", metrics.bearingWindowCalculationLatency},
        {"binCalculation", metrics.bearingBinCalculationLatency},
        {"candidateUpdate", metrics.bearingCandidateUpdateLatency},
        {"closeWindow", metrics.bearingCloseWindowLatency},
        {"snapshotBuild", metrics.bearingSnapshotBuildLatency},
        {"estimateCalculation", metrics.bearingEstimateCalculationLatency},
    };
}

void printSpectrumInternalBottleneck(
    const pipeline::PipelineMetricsSnapshot& metrics)
{
    const auto stages = spectrumInternalLatencies(metrics);
    const auto hasRecordedLatency =
        std::any_of(stages.begin(), stages.end(), [](const LatencyStage& stage) {
            return stage.stats.count > 0;
        });
    if (!hasRecordedLatency) {
        std::cout << "SpectrumAggregator internal bottleneck by avg: n/a\n"
                  << "SpectrumAggregator internal bottleneck by max: n/a\n";
        return;
    }

    const auto byAverage = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.averageMs() < right.stats.averageMs();
        });
    const auto byMax = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.maxMs < right.stats.maxMs;
        });

    std::cout << "SpectrumAggregator internal bottleneck by avg: "
              << byAverage->name << " = " << byAverage->stats.averageMs() << " ms\n"
              << "SpectrumAggregator internal bottleneck by max: " << byMax->name
              << " = " << byMax->stats.maxMs << " ms\n";
}

void printBearingInternalBottleneck(
    const pipeline::PipelineMetricsSnapshot& metrics)
{
    const auto stages = bearingInternalLatencies(metrics);
    const auto hasRecordedLatency =
        std::any_of(stages.begin(), stages.end(), [](const LatencyStage& stage) {
            return stage.stats.count > 0;
        });
    if (!hasRecordedLatency) {
        std::cout << "BearingAggregator internal bottleneck by avg: n/a\n"
                  << "BearingAggregator internal bottleneck by max: n/a\n";
        return;
    }

    const auto byAverage = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.averageMs() < right.stats.averageMs();
        });
    const auto byMax = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.maxMs < right.stats.maxMs;
        });

    std::cout << "BearingAggregator internal bottleneck by avg: "
              << byAverage->name << " = " << byAverage->stats.averageMs() << " ms\n"
              << "BearingAggregator internal bottleneck by max: " << byMax->name
              << " = " << byMax->stats.maxMs << " ms\n";
}

void printSignalParameterInternalBottleneck(
    const pipeline::PipelineMetricsSnapshot& metrics)
{
    const auto stages = signalParameterInternalLatencies(metrics);
    const auto hasRecordedLatency =
        std::any_of(stages.begin(), stages.end(), [](const LatencyStage& stage) {
            return stage.stats.count > 0;
        });
    if (!hasRecordedLatency) {
        std::cout << "SignalParameterAggregator internal bottleneck by avg: n/a\n"
                  << "SignalParameterAggregator internal bottleneck by max: n/a\n";
        return;
    }

    const auto byAverage = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.averageMs() < right.stats.averageMs();
        });
    const auto byMax = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.maxMs < right.stats.maxMs;
        });

    std::cout << "SignalParameterAggregator internal bottleneck by avg: "
              << byAverage->name << " = " << byAverage->stats.averageMs() << " ms\n"
              << "SignalParameterAggregator internal bottleneck by max: " << byMax->name
              << " = " << byMax->stats.maxMs << " ms\n";
}

void printBottleneckHint(const AuditResult& result)
{
    const auto stages = stageLatencies(result.pipeline);
    const auto hasRecordedLatency =
        std::any_of(stages.begin(), stages.end(), [](const LatencyStage& stage) {
            return stage.stats.count > 0;
        });
    if (!hasRecordedLatency) {
        std::cout << "Likely bottleneck stage by avg latency: n/a\n"
                  << "Likely bottleneck stage by max latency: n/a\n";
        return;
    }

    const auto byAverage = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.averageMs() < right.stats.averageMs();
        });
    const auto byMax = std::max_element(
        stages.begin(), stages.end(), [](const LatencyStage& left, const LatencyStage& right) {
            return left.stats.maxMs < right.stats.maxMs;
        });

    std::cout << "Likely bottleneck stage by avg latency: " << byAverage->name
              << " (" << byAverage->stats.averageMs() << " ms)\n"
              << "Likely bottleneck stage by max latency: " << byMax->name
              << " (" << byMax->stats.maxMs << " ms)\n";
    if (std::string{byAverage->name} == "SpectrumAggregator"
        || std::string{byMax->name} == "SpectrumAggregator") {
        printSpectrumInternalBottleneck(result.pipeline);
    }
    if (std::string{byAverage->name} == "BearingAggregator"
        || std::string{byMax->name} == "BearingAggregator") {
        printBearingInternalBottleneck(result.pipeline);
    }
    if (std::string{byAverage->name} == "SignalParameterAggregator"
        || std::string{byMax->name} == "SignalParameterAggregator") {
        printSignalParameterInternalBottleneck(result.pipeline);
    }
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
    hardware::SimulatorLoadProfile profile,
    AuditPipelineSizing sizing = AuditPipelineSizing::Default)
{
    pipeline::DataIngestPipelineConfig config;
    config.blockPool = pipeline::SignalBlockPoolConfig{256, 20'000};
    config.queueCapacity = 256;
    if (profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps) {
        if (sizing == AuditPipelineSizing::FullTargetRawSustain) {
            config.blockPool = pipeline::SignalBlockPoolConfig{512, 80'000};
            config.queueCapacity = 512;
        } else {
            config.blockPool = pipeline::SignalBlockPoolConfig{4, 80'000};
            config.queueCapacity = 2;
        }
    }
    config.diagnosticsPublishInterval = std::chrono::milliseconds{1000};
    config.acceptingOnStart = true;

    config.waterfall.renderBinCount = kRenderBinCount;
    config.waterfall.sourceMinHz = kSourceMinHz;
    config.waterfall.sourceMaxHz = kSourceMaxHz;
    config.waterfall.rowPeriodNs = kRowPeriodNs;
    config.waterfall.timeBase = timeBase;
    config.waterfallRows = pipeline::WaterfallRowQueueConfig{
        sizing == AuditPipelineSizing::FullTargetRawSustain ? 16'384ULL : 4096ULL,
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

pipeline::SourceToPipelineBridgeConfig makeBridgeConfig(
    hardware::SimulatorLoadProfile profile,
    AuditPipelineSizing sizing = AuditPipelineSizing::Default)
{
    pipeline::SourceToPipelineBridgeConfig config;
    if (profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps) {
        config.queueCapacity =
            sizing == AuditPipelineSizing::FullTargetRawSustain ? 64 : 2;
    } else {
        config.queueCapacity = 128;
    }
    config.overflowPolicy = pipeline::RxOverflowPolicy::DropNewest;
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
                     std::optional<std::uint64_t> maxPipelineIngestedBlocks = std::nullopt,
                     AuditPipelineSizing sizing = AuditPipelineSizing::Default)
{
    AuditResult result;
    result.profileName = profileName(profile);
    result.duration = duration;

    const auto streamConfig = makeStreamConfig(profile);
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(streamConfig.timeBase,
                                                                 profile,
                                                                 sizing));
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

    pipeline::SourceToPipelineBridge bridge(&dataPipeline,
                                            makeBridgeConfig(profile, sizing));
    const auto bridgeStarted = bridge.start();
    result.bridgeStarted = bridgeStarted.success;
    if (!bridgeStarted) {
        dataPipeline.stop();
        return result;
    }

    std::atomic_uint64_t submittedBlocks{0};

    const auto sourceStarted = source.start([&](hardware::IBcoStreamSource::SampleBlockPtr block) {
        if (!block) {
            return;
        }
        if (maxPipelineIngestedBlocks) {
            auto current = submittedBlocks.load(std::memory_order_relaxed);
            while (current < *maxPipelineIngestedBlocks) {
                if (submittedBlocks.compare_exchange_weak(current,
                                                          current + 1,
                                                          std::memory_order_relaxed)) {
                    bridge.submit(std::move(block));
                    return;
                }
            }
        } else {
            bridge.submit(std::move(block));
        }
    });
    result.sourceStarted = sourceStarted.success;

    if (sourceStarted) {
        std::this_thread::sleep_for(duration);
    }

    const auto sourceStopped = source.stop();
    result.sourceStopped = sourceStopped.success;

    const auto bridgeFlushed = bridge.flush(flushTimeout);
    result.bridgeFlushed = bridgeFlushed.success;
    bridge.stop();

    const auto flushed = dataPipeline.flushProcessing(flushTimeout);
    result.flushed = flushed.success;

    const auto drainedRows = dataPipeline.drainWaterfallRows(1'000'000);
    result.drainedWaterfallRows = drainedRows.size();
    result.source = source.metrics();
    result.bridge = bridge.metrics();
    result.pipeline = dataPipeline.metricsSnapshot();
    result.waterfallRows = dataPipeline.waterfallRowQueueMetrics();
    result.rejectedBlocks = result.bridge.rejectedBlocks;
    result.rejectedSamples = result.bridge.rejectedSamples;
    result.hasSpectrumSnapshot = dataPipeline.latestSpectrumSnapshot() != nullptr;
    result.hasBearingSnapshot = dataPipeline.latestBearingSnapshot() != nullptr;
    result.hasSignalParameterSnapshot =
        dataPipeline.latestSignalParameterSnapshot() != nullptr;

    if (!result.flushed) {
        dataPipeline.clearQueuedBlocks();
    }
    dataPipeline.stop();
    return result;
}

void printAuditSummary(const AuditResult& result)
{
    const double sourceRawThroughputTargetRatio = ratioOrZero(
        result.source.producedRawBytesPerSecond,
        static_cast<double>(result.source.targetBytesPerSecond));
    const double bridgeIngestedToReceivedRatio = ratioOrZero(
        static_cast<double>(result.bridge.ingestedSamples),
        static_cast<double>(result.bridge.receivedSamples));
    const double pipelineProcessedToInputRatio = ratioOrZero(
        static_cast<double>(result.pipeline.processedSamples),
        static_cast<double>(result.pipeline.inputSamples));
    const double snapshotsPerProcessedBlock = ratioOrZero(
        static_cast<double>(result.pipeline.producedSignalParameterSnapshots),
        static_cast<double>(result.pipeline.processedBlocks));

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "High-load data plane audit:\n"
              << "  profile = " << result.profileName << '\n'
              << "  durationSec = " << result.duration.count() << '\n'
              << "  strictNoDropMode = "
              << (strictTargetRawPipelineSustainRequired() ? "true" : "false")
              << '\n'
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
              << "  source rawThroughputTargetRatio = "
              << sourceRawThroughputTargetRatio << '\n'
              << "  source scheduleLagMaxMs = "
              << result.source.scheduleLagMax.count() << '\n'
              << "  source missedBatchDeadlines = "
              << result.source.missedBatchDeadlines << '\n'
              << "  source simulatorBackpressureEvents = "
              << result.source.simulatorBackpressureEvents << '\n'
              << "  source maxCallbackDurationMs = "
              << result.source.maxCallbackDuration.count() << '\n'
              << "  bridge receivedBlocks = " << result.bridge.receivedBlocks << '\n'
              << "  bridge receivedSamples = " << result.bridge.receivedSamples << '\n'
              << "  bridge enqueuedBlocks = " << result.bridge.enqueuedBlocks << '\n'
              << "  bridge enqueuedSamples = " << result.bridge.enqueuedSamples << '\n'
              << "  bridge droppedBlocks = " << result.bridge.droppedBlocks << '\n'
              << "  bridge droppedSamples = " << result.bridge.droppedSamples << '\n'
              << "  bridge ingestedBlocks = " << result.bridge.ingestedBlocks << '\n'
              << "  bridge ingestedSamples = " << result.bridge.ingestedSamples << '\n'
              << "  bridge rejectedBlocks = " << result.bridge.rejectedBlocks << '\n'
              << "  bridge rejectedSamples = " << result.bridge.rejectedSamples << '\n'
              << "  bridge ingestedToReceivedRatio = "
              << bridgeIngestedToReceivedRatio << '\n'
              << "  bridge queueDepth = " << result.bridge.queueDepth << '\n'
              << "  bridge queueCapacity = " << result.bridge.queueCapacity << '\n'
              << "  bridge enqueueLatencyMaxMs = "
              << result.bridge.enqueueLatencyMax.count() << '\n'
              << "  bridge ingestLatencyMaxMs = "
              << result.bridge.ingestLatencyMax.count() << '\n'
              << "  pipeline inputBlocks = " << result.pipeline.inputBlocks << '\n'
              << "  pipeline inputSamples = " << result.pipeline.inputSamples << '\n'
              << "  pipeline processedBlocks = " << result.pipeline.processedBlocks
              << '\n'
              << "  pipeline processedSamples = " << result.pipeline.processedSamples
              << '\n'
              << "  pipeline processedToInputRatio = "
              << pipelineProcessedToInputRatio << '\n'
              << "  pipeline droppedSamples = " << result.pipeline.droppedSamples << '\n'
              << "  pipeline droppedBlocks = " << result.pipeline.droppedBlocks << '\n'
              << "  pipeline inputMBps = "
              << result.pipeline.inputMegabytesPerSecond << '\n'
              << "  pipeline processedMBps = "
              << result.pipeline.processedMegabytesPerSecond << '\n'
              << "  queueDepth = " << result.pipeline.queueDepth << '\n'
              << "  queueCapacity = " << result.pipeline.queueCapacity << '\n'
              << "  queuePushedBlocks = " << result.pipeline.queuePushedBlocks << '\n'
              << "  queuePoppedBlocks = " << result.pipeline.queuePoppedBlocks << '\n'
              << "  queueDroppedBlocks = " << result.pipeline.queueDroppedBlocks << '\n'
              << "  blockPoolCapacity = " << result.pipeline.blockPoolCapacity << '\n'
              << "  blockPoolAvailable = " << result.pipeline.blockPoolAvailable << '\n'
              << "  blockPoolInUse = " << result.pipeline.blockPoolInUse << '\n'
              << "  blockPoolExhausted = " << result.pipeline.blockPoolExhausted << '\n'
              << "  blockPoolUsage = " << result.pipeline.blockPoolUsage << '\n'
              << "  maxBlockAgeMs = " << result.pipeline.maxBlockAgeMs << '\n'
              << "  processingLatencyMaxMs = "
              << result.pipeline.processingLatencyMaxMs << '\n'
              << "Processing latency breakdown:\n"
              << "  totalProcessBlockMs avg/max = "
              << result.pipeline.processBlockLatency.averageMs() << "/"
              << result.pipeline.processBlockLatency.maxMs << '\n'
              << "  waterfallMs avg/max = "
              << result.pipeline.waterfallAggregationLatency.averageMs() << "/"
              << result.pipeline.waterfallAggregationLatency.maxMs << '\n'
              << "  spectrumMs avg/max = "
              << result.pipeline.spectrumAggregationLatency.averageMs() << "/"
              << result.pipeline.spectrumAggregationLatency.maxMs << '\n'
              << "  bearingMs avg/max = "
              << result.pipeline.bearingAggregationLatency.averageMs() << "/"
              << result.pipeline.bearingAggregationLatency.maxMs << '\n'
              << "  signalParametersMs avg/max = "
              << result.pipeline.signalParameterAggregationLatency.averageMs() << "/"
              << result.pipeline.signalParameterAggregationLatency.maxMs << '\n'
              << "  waterfallRowPublishMs avg/max = "
              << result.pipeline.waterfallRowPublishLatency.averageMs() << "/"
              << result.pipeline.waterfallRowPublishLatency.maxMs << '\n'
              << "  spectrumSnapshotPublishMs avg/max = "
              << result.pipeline.spectrumSnapshotPublishLatency.averageMs() << "/"
              << result.pipeline.spectrumSnapshotPublishLatency.maxMs << '\n'
              << "  bearingSnapshotPublishMs avg/max = "
              << result.pipeline.bearingSnapshotPublishLatency.averageMs() << "/"
              << result.pipeline.bearingSnapshotPublishLatency.maxMs << '\n'
              << "  signalParameterSnapshotPublishMs avg/max = "
              << result.pipeline.signalParameterSnapshotPublishLatency.averageMs() << "/"
              << result.pipeline.signalParameterSnapshotPublishLatency.maxMs << '\n'
              << "Spectrum micro-breakdown:\n"
              << "  sampleLoop avg/max = "
              << result.pipeline.spectrumSampleLoopLatency.averageMs() << "/"
              << result.pipeline.spectrumSampleLoopLatency.maxMs << '\n'
              << "  windowCalculation avg/max = "
              << result.pipeline.spectrumWindowCalculationLatency.averageMs() << "/"
              << result.pipeline.spectrumWindowCalculationLatency.maxMs << '\n'
              << "  binCalculation avg/max = "
              << result.pipeline.spectrumBinCalculationLatency.averageMs() << "/"
              << result.pipeline.spectrumBinCalculationLatency.maxMs << '\n'
              << "  binUpdate avg/max = "
              << result.pipeline.spectrumBinUpdateLatency.averageMs() << "/"
              << result.pipeline.spectrumBinUpdateLatency.maxMs << '\n'
              << "  bandSummaryUpdate avg/max = "
              << result.pipeline.spectrumBandSummaryUpdateLatency.averageMs() << "/"
              << result.pipeline.spectrumBandSummaryUpdateLatency.maxMs << '\n'
              << "  closeWindow avg/max = "
              << result.pipeline.spectrumCloseWindowLatency.averageMs() << "/"
              << result.pipeline.spectrumCloseWindowLatency.maxMs << '\n'
              << "  snapshotBuild avg/max = "
              << result.pipeline.spectrumSnapshotBuildLatency.averageMs() << "/"
              << result.pipeline.spectrumSnapshotBuildLatency.maxMs << '\n'
              << "  usedFastWindowIndex = "
              << (result.pipeline.spectrumFastWindowBlocks > 0 ? "true" : "false")
              << '\n'
              << "  usedFastBinIndex = "
              << (result.pipeline.spectrumFastBinBlocks > 0 ? "true" : "false")
              << '\n'
              << "  usedFastBandSummaryStorage = "
              << (result.pipeline.spectrumFastBandSummaryBlocks > 0 ? "true"
                                                                    : "false")
              << '\n'
              << "  spectrumFastWindowBlocks = "
              << result.pipeline.spectrumFastWindowBlocks << '\n'
              << "  spectrumFastBinBlocks = "
              << result.pipeline.spectrumFastBinBlocks << '\n'
              << "  spectrumFastBandSummaryBlocks = "
              << result.pipeline.spectrumFastBandSummaryBlocks << '\n'
              << "Bearing micro-breakdown:\n"
              << "  sampleLoop avg/max = "
              << result.pipeline.bearingSampleLoopLatency.averageMs() << "/"
              << result.pipeline.bearingSampleLoopLatency.maxMs << '\n'
              << "  windowCalculation avg/max = "
              << result.pipeline.bearingWindowCalculationLatency.averageMs() << "/"
              << result.pipeline.bearingWindowCalculationLatency.maxMs << '\n'
              << "  binCalculation avg/max = "
              << result.pipeline.bearingBinCalculationLatency.averageMs() << "/"
              << result.pipeline.bearingBinCalculationLatency.maxMs << '\n'
              << "  candidateUpdate avg/max = "
              << result.pipeline.bearingCandidateUpdateLatency.averageMs() << "/"
              << result.pipeline.bearingCandidateUpdateLatency.maxMs << '\n'
              << "  closeWindow avg/max = "
              << result.pipeline.bearingCloseWindowLatency.averageMs() << "/"
              << result.pipeline.bearingCloseWindowLatency.maxMs << '\n'
              << "  snapshotBuild avg/max = "
              << result.pipeline.bearingSnapshotBuildLatency.averageMs() << "/"
              << result.pipeline.bearingSnapshotBuildLatency.maxMs << '\n'
              << "  estimateCalculation avg/max = "
              << result.pipeline.bearingEstimateCalculationLatency.averageMs() << "/"
              << result.pipeline.bearingEstimateCalculationLatency.maxMs << '\n'
              << "  usedFastCandidateStorage = "
              << (result.pipeline.bearingFastCandidateStorageBlocks > 0 ? "true"
                                                                        : "false")
              << '\n'
              << "  bearingFastCandidateStorageBlocks = "
              << result.pipeline.bearingFastCandidateStorageBlocks << '\n'
              << "Signal parameter micro-breakdown:\n"
              << "  ingest avg/max = "
              << result.pipeline.signalParameterIngestLatency.averageMs() << "/"
              << result.pipeline.signalParameterIngestLatency.maxMs << '\n'
              << "  snapshotDecision avg/max = "
              << result.pipeline.signalParameterSnapshotDecisionLatency.averageMs() << "/"
              << result.pipeline.signalParameterSnapshotDecisionLatency.maxMs << '\n'
              << "  finalize avg/max = "
              << result.pipeline.signalParameterFinalizeLatency.averageMs() << "/"
              << result.pipeline.signalParameterFinalizeLatency.maxMs << '\n'
              << "  snapshotBuild avg/max = "
              << result.pipeline.signalParameterSnapshotBuildLatency.averageMs() << "/"
              << result.pipeline.signalParameterSnapshotBuildLatency.maxMs << '\n'
              << "  producedSignalParameterSnapshots = "
              << result.pipeline.producedSignalParameterSnapshots << '\n'
              << "  signalParameterTrustedFixedBandFastPath = "
              << (result.pipeline.signalParameterTrustedFixedBandFastPathBlocks > 0 ? "true"
                                                                                    : "false")
              << '\n'
              << "  signalParameterTrustedFixedBandFastPathBlocks = "
              << result.pipeline.signalParameterTrustedFixedBandFastPathBlocks << '\n'
              << "  processedBlocks = " << result.pipeline.processedBlocks << '\n'
              << "  snapshotsPerProcessedBlock = " << snapshotsPerProcessedBlock << '\n'
              << "  processedBlocks = " << result.pipeline.processedBlocks << '\n'
              << "  processedSamples = "
              << result.pipeline.processedBlockSamplesTotal << '\n'
              << "  avgSamplesPerBlock = "
              << result.pipeline.averageSamplesPerProcessedBlock << '\n'
              << "  producedWaterfallRows = "
              << result.pipeline.producedWaterfallRows << '\n'
              << "  drainedWaterfallRows = " << result.drainedWaterfallRows << '\n'
              << "  waterfallDroppedRows = "
              << result.pipeline.waterfallDroppedRows << '\n'
              << "  producedSpectrumSnapshots = "
              << result.pipeline.producedSpectrumSnapshots << '\n'
              << "  producedBearingSnapshots = "
              << result.pipeline.producedBearingSnapshots << '\n'
              << "  producedSignalParameterSnapshots = "
              << result.pipeline.producedSignalParameterSnapshots << '\n'
              << "  hasSpectrumSnapshot = "
              << (result.hasSpectrumSnapshot ? "true" : "false") << '\n'
              << "  hasBearingSnapshot = "
              << (result.hasBearingSnapshot ? "true" : "false") << '\n'
              << "  hasSignalParameterSnapshot = "
              << (result.hasSignalParameterSnapshot ? "true" : "false") << '\n'
              << "  rejectedBlocks = " << result.rejectedBlocks << '\n'
              << "  rejectedSamples = " << result.rejectedSamples << '\n';
    printBottleneckHint(result);
}

void printSustainWarning(const std::string& message)
{
    std::cout << "WARNING: " << message << '\n';
}

void printSustainWarnings(const AuditResult& result)
{
    if (result.bridge.droppedBlocks > 0) {
        printSustainWarning("bridge dropped RX blocks: blocks="
                            + std::to_string(result.bridge.droppedBlocks)
                            + " samples="
                            + std::to_string(result.bridge.droppedSamples));
    }
    if (result.pipeline.droppedBlocks > 0) {
        printSustainWarning("pipeline dropped input blocks: blocks="
                            + std::to_string(result.pipeline.droppedBlocks)
                            + " samples="
                            + std::to_string(result.pipeline.droppedSamples));
    }
    if (result.pipeline.queueDroppedBlocks > 0) {
        printSustainWarning("bounded pipeline queue dropped blocks: "
                            + std::to_string(result.pipeline.queueDroppedBlocks));
    }
    if (result.pipeline.processedSamples < result.pipeline.inputSamples) {
        printSustainWarning("pipeline processed fewer samples than it accepted: input="
                            + std::to_string(result.pipeline.inputSamples)
                            + " processed="
                            + std::to_string(result.pipeline.processedSamples));
    }
    if (result.source.simulatorBackpressureEvents > 0) {
        printSustainWarning("source reported simulator backpressure events: "
                            + std::to_string(result.source.simulatorBackpressureEvents));
    }
    if (result.source.missedBatchDeadlines > 0) {
        printSustainWarning("source missed batch deadlines: "
                            + std::to_string(result.source.missedBatchDeadlines));
    }
    if (result.pipeline.waterfallDroppedRows > 0) {
        printSustainWarning("waterfall row queue dropped rows: "
                            + std::to_string(result.pipeline.waterfallDroppedRows));
    }
}

void assertAuditSucceeded(TestRunner& test, const AuditResult& result)
{
    test.require(result.sourceConfigured, "source accepts stream config");
    test.require(result.pipelineStarted, "data ingest pipeline starts");
    test.require(result.bridgeStarted, "source-to-pipeline bridge starts");
    test.require(result.sourceStarted, "high-load source starts");
    test.require(result.sourceStopped, "high-load source stops");
    test.require(result.bridgeFlushed, "source-to-pipeline bridge flushes");
    test.require(result.flushed, "data ingest pipeline flushes");

    test.require(result.source.producedSamples > 0, "source produces samples");
    test.require(result.source.producedBatches > 0, "source produces batches");
    test.require(result.source.producedSamplesPerSecond > 0.0,
                 "source reports positive sample throughput");
    test.require(result.source.equivalentMegabytesPerSecond > 0.0,
                 "source reports positive equivalent MBps");
    test.require(result.source.maxCallbackDuration.count() < 5000,
                 "source callback duration stays bounded");

    test.require(result.bridge.receivedBlocks > 0, "bridge receives source blocks");
    test.require(result.bridge.enqueuedBlocks > 0, "bridge enqueues source blocks");
    test.require(result.bridge.droppedBlocks == 0,
                 "bridge reports no dropped blocks in medium smoke");
    test.require(result.bridge.rejectedBlocks == 0,
                 "bridge reports no rejected blocks in medium smoke");
    test.require(result.bridge.ingestedBlocks == result.bridge.enqueuedBlocks,
                 "bridge ingests every enqueued block");
    test.require(result.bridge.ingestedSamples == result.pipeline.inputSamples,
                 "bridge ingested samples match pipeline input samples");
    test.require(result.bridge.queueDepth == 0, "bridge queue drains after flush");

    test.require(result.pipeline.inputSamples > 0, "pipeline receives samples");
    test.require(result.pipeline.processedSamples > 0, "pipeline processes samples");
    test.require(result.pipeline.inputMegabytesPerSecond > 0.0,
                 "pipeline reports positive input MBps");
    test.require(result.pipeline.processedMegabytesPerSecond > 0.0,
                 "pipeline reports positive processed MBps");
    test.require(result.pipeline.processedSamples == result.pipeline.inputSamples,
                 "pipeline processes every accepted sample");

    test.require(result.rejectedBlocks == 0, "bridge does not reject blocks");
    test.require(result.rejectedSamples == 0, "bridge does not reject samples");
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
    test.require(result.pipeline.producedSignalParameterSnapshots > 0,
                 "pipeline metrics count produced signal parameter snapshots");
}

void assertTargetRawPipelineSustain(TestRunner& test, const AuditResult& result)
{
    test.require(result.sourceConfigured,
                 "target raw full pipeline source accepts stream config");
    test.require(result.pipelineStarted,
                 "target raw full pipeline data ingest pipeline starts");
    test.require(result.bridgeStarted,
                 "target raw full pipeline source-to-pipeline bridge starts");
    test.require(result.sourceStarted,
                 "target raw full pipeline high-load source starts");
    test.require(result.sourceStopped,
                 "target raw full pipeline high-load source stops");
    test.require(result.bridgeFlushed,
                 "target raw full pipeline source-to-pipeline bridge flushes");

    test.require(result.source.targetBytesPerSecond == kTargetRawBytesPerSecond,
                 "target raw full pipeline source reports configured raw byte target");
    test.require(result.source.producedSamples > 0,
                 "target raw full pipeline source produces samples");
    test.require(result.source.producedBatches > 0,
                 "target raw full pipeline source produces batches");
    test.require(result.source.producedRawBytes > 0,
                 "target raw full pipeline source reports produced raw bytes");
    test.require(result.source.producedRawBytesPerSecond > 0.0,
                 "target raw full pipeline source reports positive raw throughput");
    test.require(result.source.producedParsedSamplesPerSecond > 0.0,
                 "target raw full pipeline source reports positive parsed sample throughput");

    test.require(result.bridge.receivedBlocks > 0,
                 "target raw full pipeline bridge receives source blocks");
    test.require(result.bridge.receivedSamples > 0,
                 "target raw full pipeline bridge receives source samples");
    test.require(result.bridge.enqueuedBlocks > 0,
                 "target raw full pipeline bridge enqueues source blocks");
    test.require(result.bridge.ingestedBlocks > 0,
                 "target raw full pipeline bridge ingests source blocks");
    test.require(result.bridge.ingestedSamples > 0,
                 "target raw full pipeline bridge ingests source samples");

    test.require(result.pipeline.inputSamples > 0,
                 "target raw full pipeline receives samples");
    test.require(result.pipeline.processedSamples > 0,
                 "target raw full pipeline processes samples");
    test.require(result.pipeline.producedWaterfallRows > 0,
                 "target raw full pipeline produces waterfall rows");

    test.require(result.hasSpectrumSnapshot,
                 "target raw full pipeline publishes spectrum snapshot");
    test.require(result.hasSignalParameterSnapshot,
                 "target raw full pipeline publishes signal parameter snapshot");
    test.require(result.pipeline.producedSignalParameterSnapshots > 0,
                 "target raw full pipeline metrics count signal parameter snapshots");
    if (result.pipeline.producedBearingSnapshots > 0) {
        test.require(result.hasBearingSnapshot,
                     "target raw full pipeline publishes bearing snapshot");
    } else {
        printSustainWarning("bearing snapshot assertion skipped because no bearing "
                            "snapshots were produced");
    }

    if (!strictTargetRawPipelineSustainRequired()) {
        return;
    }

    test.require(result.flushed,
                 "strict target raw full pipeline data ingest pipeline flushes");
    test.require(result.bridge.droppedBlocks == 0,
                 "strict target raw full pipeline bridge reports no dropped blocks");
    test.require(result.bridge.droppedSamples == 0,
                 "strict target raw full pipeline bridge reports no dropped samples");
    test.require(result.bridge.rejectedBlocks == 0,
                 "strict target raw full pipeline bridge reports no rejected blocks");
    test.require(result.bridge.rejectedSamples == 0,
                 "strict target raw full pipeline bridge reports no rejected samples");
    test.require(result.pipeline.droppedSamples == 0,
                 "strict target raw full pipeline reports no dropped samples");
    test.require(result.pipeline.droppedBlocks == 0,
                 "strict target raw full pipeline reports no dropped blocks");
    test.require(result.pipeline.queueDroppedBlocks == 0,
                 "strict target raw full pipeline queue reports no dropped blocks");
    test.require(result.pipeline.blockPoolExhausted == 0,
                 "strict target raw full pipeline block pool is not exhausted");
    test.require(result.pipeline.processedSamples == result.pipeline.inputSamples,
                 "strict target raw full pipeline processes every accepted sample");
    test.require(result.source.simulatorBackpressureEvents == 0,
                 "strict target raw full pipeline source reports no backpressure events");
}

bool stressTestsEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_STRESS_TESTS");
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

    // This is source-accounting smoke only. Full uncapped 90 MB/s pipeline sustain
    // is tested by SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST=1.
    const auto result = runAudit(std::chrono::seconds{3},
                                 hardware::SimulatorLoadProfile::
                                     TargetRawThroughput90MBps,
                                 std::chrono::milliseconds{250},
                                 std::uint64_t{1});
    printAuditSummary(result);

    test.require(result.sourceConfigured, "target raw source accepts stream config");
    test.require(result.pipelineStarted, "target raw data ingest pipeline starts");
    test.require(result.bridgeStarted, "target raw source-to-pipeline bridge starts");
    test.require(result.sourceStarted, "target raw high-load source starts");
    test.require(result.sourceStopped, "target raw high-load source stops");
    test.require(result.bridgeFlushed, "target raw source-to-pipeline bridge flushes");
    test.require(result.bridge.receivedBlocks > 0,
                 "target raw bridge receives capped source-accounting block");
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

void targetRaw90mbpsPipelineSustainAudit(TestRunner& test)
{
    if (!targetRawPipelineTestEnabled()) {
        return;
    }

    const auto result = runAudit(std::chrono::seconds{10},
                                 hardware::SimulatorLoadProfile::
                                     TargetRawThroughput90MBps,
                                 std::chrono::seconds{10},
                                 std::nullopt,
                                 AuditPipelineSizing::FullTargetRawSustain);
    printAuditSummary(result);
    printSustainWarnings(result);
    assertTargetRawPipelineSustain(test, result);
}

} // namespace

int main()
{
    TestRunner test;

    highLoadDataPlaneSmoke(test);
    targetRaw90mbpsAccountingSmoke(test);
    targetRaw90mbpsPipelineSustainAudit(test);
    highLoadDataPlaneStress(test);

    return test.result();
}
