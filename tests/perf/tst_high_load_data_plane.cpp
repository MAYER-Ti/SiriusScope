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
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
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
constexpr std::uint64_t kBaselineRawBytesPerSecond = 60'000'000;
constexpr std::uint64_t kExpectedBaselineRawBytesPerSecond = 59'856'000;
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

enum class AuditMode
{
    Smoke,
    BaselineRaw60,
    TargetRawShort,
    TargetRawStrict,
    TargetRawSoak,
};

enum class CapacityProfile
{
    Current,
    Balanced1024,
    Balanced2048,
};

struct CapacityProfileConfig
{
    CapacityProfile profile = CapacityProfile::Current;
    const char* name = "current";
    std::size_t bridgeQueueCapacity = 64;
    std::size_t pipelineQueueCapacity = 512;
    std::size_t blockPoolCapacity = 512;
    std::size_t stageQueueCapacity = 512;
};

enum class BacklogTrend
{
    Stable,
    Growing,
    Saturating,
    Unknown,
};

struct LatencyBudget
{
    bool enabled = false;
    double maxFanOutEndToEndMs = 0.0;
    double maxStageQueueWaitMs = 0.0;
    double maxStageQueueDepthRatio = 0.0;
};

struct BacklogSample
{
    double elapsedSec = 0.0;
    std::size_t waterfallDepth = 0;
    std::size_t spectrumDepth = 0;
    std::size_t bearingDepth = 0;
    std::size_t signalParameterDepth = 0;
    std::uint64_t inFlightFanOutBlocks = 0;
    std::size_t blockPoolInUse = 0;
};

struct AuditResult
{
    AuditMode auditMode = AuditMode::Smoke;
    std::string profileName;
    std::chrono::seconds duration{0};
    pipeline::ProcessingMode processingMode = pipeline::ProcessingMode::Sequential;
    LatencyBudget latencyBudget;
    bool visualBackpressureEnabled = false;
    pipeline::StageOverloadPolicy visualStagePolicy =
        pipeline::StageOverloadPolicy::LosslessRequired;
    std::chrono::milliseconds visualMaxQueueWait{1000};
    double visualMaxQueueDepthRatio = 0.50;
    bool visualBearingBestEffort = false;
    bool allowVisualDegradationInStrict = false;
    bool signalParameterStageEnabled = true;
    std::size_t batchSizeMultiplier = 1;
    CapacityProfile capacityProfile = CapacityProfile::Current;
    std::string capacityProfileName = "current";
    bool capacityProfileApplied = false;
    std::size_t bridgeQueueCapacity = 0;
    std::size_t pipelineQueueCapacity = 0;
    std::size_t blockPoolCapacity = 0;
    std::size_t stageQueueCapacity = 0;
    std::size_t samplesPerBlock = 0;
    std::size_t estimatedBlockBytes = 0;
    std::size_t estimatedPoolBytes = 0;
    bool profileSetupSucceeded = true;
    std::string profileSetupError;
    hardware::BcoSourceMetrics source;
    pipeline::SourceToPipelineBridgeMetrics bridge;
    pipeline::PipelineMetricsSnapshot pipeline;
    pipeline::WaterfallRowQueueMetrics waterfallRows;
    std::vector<BacklogSample> backlogSamples;
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

struct StageBacklog
{
    const char* name = "";
    pipeline::StageMetricsSnapshot metrics;
};

enum class AuditPipelineSizing
{
    Default,
    FullTargetRawSustain,
};

enum class BudgetStatus
{
    NotApplicable,
    Pass,
    Warn,
    Fail,
};

struct StageBudgetEvaluation
{
    const char* name = "";
    double queueMaxDepthRatio = 0.0;
    BudgetStatus queueWaitStatus = BudgetStatus::NotApplicable;
    BudgetStatus queueDepthStatus = BudgetStatus::NotApplicable;
};

struct LatencyBudgetEvaluation
{
    BudgetStatus fanOutEndToEndStatus = BudgetStatus::NotApplicable;
    std::vector<StageBudgetEvaluation> stages;

    bool hasFailures() const
    {
        if (fanOutEndToEndStatus == BudgetStatus::Fail) {
            return true;
        }
        return std::any_of(stages.begin(), stages.end(), [](const auto& stage) {
            return stage.queueWaitStatus == BudgetStatus::Fail
                || stage.queueDepthStatus == BudgetStatus::Fail;
        });
    }

    bool hasWarnings() const
    {
        if (fanOutEndToEndStatus == BudgetStatus::Warn) {
            return true;
        }
        return std::any_of(stages.begin(), stages.end(), [](const auto& stage) {
            return stage.queueWaitStatus == BudgetStatus::Warn
                || stage.queueDepthStatus == BudgetStatus::Warn;
        });
    }
};

struct BatchProfileScore
{
    std::size_t multiplier = 1;
    bool noDropPass = false;
    bool latencyBudgetPass = false;
    bool queueDepthBudgetPass = false;
    double rawMBps = 0.0;
    double fanOutMaxMs = 0.0;
    double maxQueueDepthRatio = 0.0;
    std::uint64_t rejectedBlocks = 0;
    std::uint64_t blockPoolExhausted = 0;
    std::uint64_t inputSamples = 0;
    std::uint64_t processedSamples = 0;
    std::uint64_t droppedBlocks = 0;
    std::uint64_t droppedSamples = 0;
    std::uint64_t queueDroppedBlocks = 0;
};

struct BatchProfileSelection
{
    std::optional<BatchProfileScore> selected;
    std::string reason;
};

struct CapacityBacklogTrend
{
    BacklogTrend waterfall = BacklogTrend::Unknown;
    BacklogTrend spectrum = BacklogTrend::Unknown;
    BacklogTrend bearing = BacklogTrend::Unknown;
    BacklogTrend signalParameter = BacklogTrend::Unknown;
    BacklogTrend overall = BacklogTrend::Unknown;
};

struct CapacityProfileScore
{
    CapacityProfile profile = CapacityProfile::Current;
    std::string profileName;
    bool noDropPass = false;
    bool latencyBudgetPass = false;
    bool queueDepthBudgetPass = false;
    double rawMBps = 0.0;
    double fanOutMaxMs = 0.0;
    double maxQueueDepthRatio = 0.0;
    std::uint64_t rejectedBlocks = 0;
    std::uint64_t blockPoolExhausted = 0;
    std::uint64_t inputSamples = 0;
    std::uint64_t processedSamples = 0;
    std::uint64_t droppedBlocks = 0;
    std::uint64_t droppedSamples = 0;
    std::uint64_t queueDroppedBlocks = 0;
    CapacityBacklogTrend backlogTrend;
};

struct CapacityProfileSelection
{
    std::optional<CapacityProfileScore> selected;
    std::string reason;
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

std::uint64_t effectiveRawBytesPerSecondFor(const hardware::ThroughputTarget& target)
{
    const auto samplesPerBatch = hardware::samplesPerBatchForTarget(target);
    const auto rawBytesPerBatch =
        hardware::rawBytesForSamples(samplesPerBatch, target.packetModel);
    const auto batchPeriodMs = std::max<std::int64_t>(1, target.batchPeriod.count());
    return rawBytesPerBatch * 1000ULL / static_cast<std::uint64_t>(batchPeriodMs);
}

double ratioOrZero(double numerator, double denominator)
{
    if (denominator <= 0.0) {
        return 0.0;
    }

    return numerator / denominator;
}

std::size_t saturatedMultiplySize(std::size_t value, std::size_t multiplier)
{
    if (value == 0 || multiplier == 0) {
        return 0;
    }
    if (multiplier > std::numeric_limits<std::size_t>::max() / value) {
        return std::numeric_limits<std::size_t>::max();
    }
    return value * multiplier;
}

bool isAllowedBatchMultiplier(std::size_t multiplier)
{
    return multiplier == 1 || multiplier == 2 || multiplier == 4 || multiplier == 8;
}

std::size_t normalizeBatchMultiplier(std::size_t multiplier)
{
    return isAllowedBatchMultiplier(multiplier) ? multiplier : 1;
}

std::size_t scaledCapacity(std::size_t base,
                           std::size_t multiplier,
                           std::size_t minimum)
{
    const auto safeMultiplier = normalizeBatchMultiplier(multiplier);
    return std::max<std::size_t>(minimum, base / safeMultiplier);
}

const char* capacityProfileName(CapacityProfile profile)
{
    switch (profile) {
    case CapacityProfile::Current:
        return "current";
    case CapacityProfile::Balanced1024:
        return "balanced1024";
    case CapacityProfile::Balanced2048:
        return "balanced2048";
    }

    return "current";
}

int capacityProfileRank(CapacityProfile profile)
{
    switch (profile) {
    case CapacityProfile::Current:
        return 0;
    case CapacityProfile::Balanced1024:
        return 1;
    case CapacityProfile::Balanced2048:
        return 2;
    }

    return 0;
}

CapacityProfileConfig capacityProfileConfig(CapacityProfile profile,
                                            std::size_t batchSizeMultiplier)
{
    switch (profile) {
    case CapacityProfile::Balanced1024:
        return CapacityProfileConfig{
            profile,
            "balanced1024",
            128,
            1024,
            1024,
            1024,
        };
    case CapacityProfile::Balanced2048:
        return CapacityProfileConfig{
            profile,
            "balanced2048",
            256,
            2048,
            2048,
            2048,
        };
    case CapacityProfile::Current:
        break;
    }

    return CapacityProfileConfig{
        CapacityProfile::Current,
        "current",
        scaledCapacity(64, batchSizeMultiplier, 8),
        scaledCapacity(512, batchSizeMultiplier, 64),
        scaledCapacity(512, batchSizeMultiplier, 64),
        scaledCapacity(512, batchSizeMultiplier, 64),
    };
}

std::size_t targetRawSamplesPerBatch(std::size_t batchSizeMultiplier)
{
    return saturatedMultiplySize(
        hardware::samplesPerBatchForTarget(targetRaw90Mbps()),
        normalizeBatchMultiplier(batchSizeMultiplier));
}

std::size_t targetRawMaxSamplesPerBlock(std::size_t batchSizeMultiplier)
{
    return std::max<std::size_t>(80'000,
                                 targetRawSamplesPerBatch(batchSizeMultiplier));
}

std::size_t estimatedBlockBytes(std::size_t samplesPerBlock)
{
    return saturatedMultiplySize(sizeof(core::SignalSample), samplesPerBlock);
}

std::size_t estimatedPoolBytes(std::size_t blockCount, std::size_t samplesPerBlock)
{
    return saturatedMultiplySize(blockCount, estimatedBlockBytes(samplesPerBlock));
}

double producedBatchesPerSecond(const AuditResult& result)
{
    return ratioOrZero(static_cast<double>(result.source.producedBatches),
                       static_cast<double>(result.duration.count()));
}

double producedSamplesPerBatch(const AuditResult& result)
{
    return ratioOrZero(static_cast<double>(result.source.producedSamples),
                       static_cast<double>(result.source.producedBatches));
}

double parallelFanOutBlocksPerSecond(const AuditResult& result)
{
    return ratioOrZero(static_cast<double>(result.pipeline.parallelFanOutBlocks),
                       static_cast<double>(result.duration.count()));
}

double stageBlocksPerSecond(const AuditResult& result,
                            const pipeline::StageMetricsSnapshot& metrics)
{
    return ratioOrZero(static_cast<double>(metrics.processedBlocks),
                       static_cast<double>(result.duration.count()));
}

std::optional<int> parsePositiveInt(const char* text)
{
    if (!text || *text == '\0') {
        return std::nullopt;
    }

    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < 1
        || parsed > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    return static_cast<int>(parsed);
}

std::optional<std::size_t> parseBatchMultiplier(const char* text)
{
    const auto parsed = parsePositiveInt(text);
    if (!parsed) {
        return std::nullopt;
    }

    const auto multiplier = static_cast<std::size_t>(*parsed);
    if (!isAllowedBatchMultiplier(multiplier)) {
        return std::nullopt;
    }
    return multiplier;
}

std::optional<double> parsePositiveDouble(const char* text)
{
    if (!text || *text == '\0') {
        return std::nullopt;
    }

    errno = 0;
    char* end = nullptr;
    const auto parsed = std::strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !std::isfinite(parsed)
        || parsed <= 0.0) {
        return std::nullopt;
    }

    return parsed;
}

std::optional<double> parseDepthRatio(const char* text)
{
    const auto parsed = parsePositiveDouble(text);
    if (!parsed || *parsed > 1.0) {
        return std::nullopt;
    }
    return parsed;
}

int envPositiveIntOr(const char* name, int defaultValue)
{
    return parsePositiveInt(std::getenv(name)).value_or(defaultValue);
}

double envPositiveDoubleOr(const char* name, double defaultValue)
{
    return parsePositiveDouble(std::getenv(name)).value_or(defaultValue);
}

double envDepthRatioOr(const char* name, double defaultValue)
{
    return parseDepthRatio(std::getenv(name)).value_or(defaultValue);
}

std::optional<pipeline::StageOverloadPolicy> parseVisualStagePolicy(const char* text)
{
    if (!text || *text == '\0') {
        return std::nullopt;
    }

    const std::string value(text);
    if (value == "latest-only") {
        return pipeline::StageOverloadPolicy::RealtimeLatestOnly;
    }
    if (value == "drop-oldest") {
        return pipeline::StageOverloadPolicy::BoundedLatencyDropOldest;
    }
    return std::nullopt;
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

bool visualBackpressurePolicyEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_ENABLE_VISUAL_BACKPRESSURE_POLICY");
}

pipeline::StageOverloadPolicy envVisualStagePolicyOrDefault()
{
    const char* value = std::getenv("SIRIUSSCOPE_VISUAL_STAGE_POLICY");
    if (!value) {
        return pipeline::StageOverloadPolicy::RealtimeLatestOnly;
    }

    const auto parsed = parseVisualStagePolicy(value);
    if (parsed) {
        return *parsed;
    }

    std::cout << "WARNING: invalid SIRIUSSCOPE_VISUAL_STAGE_POLICY='"
              << value << "', using latest-only\n";
    return pipeline::StageOverloadPolicy::RealtimeLatestOnly;
}

std::chrono::milliseconds envVisualMaxQueueWait()
{
    return std::chrono::milliseconds{
        envPositiveIntOr("SIRIUSSCOPE_VISUAL_MAX_QUEUE_WAIT_MS", 1000),
    };
}

double envVisualMaxQueueDepthRatio()
{
    return envDepthRatioOr("SIRIUSSCOPE_VISUAL_MAX_QUEUE_DEPTH_RATIO", 0.50);
}

bool visualBearingBestEffortEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_VISUAL_BEARING_BEST_EFFORT");
}

bool visualDegradationAllowedInStrict()
{
    return envFlagEnabled("SIRIUSSCOPE_ALLOW_VISUAL_DEGRADATION_IN_STRICT");
}

std::size_t envBatchMultiplierOrDefault()
{
    const char* value = std::getenv("SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER");
    if (!value) {
        return 1;
    }

    const auto parsed = parseBatchMultiplier(value);
    if (parsed) {
        return *parsed;
    }

    std::cout << "WARNING: invalid SIRIUSSCOPE_90MBPS_BATCH_MULTIPLIER='"
              << value << "', using 1\n";
    return 1;
}

bool targetRawBatchSweepEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_90MBPS_BATCH_SWEEP");
}

bool targetRawProfileSelectionEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_90MBPS_PROFILE_SELECTION");
}

bool targetRawCapacitySweepEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_90MBPS_CAPACITY_SWEEP");
}

bool signalParameterStageDisabledByEnv()
{
    return envFlagEnabled("SIRIUSSCOPE_DISABLE_SIGNAL_PARAMETER_STAGE");
}

bool targetRawSignalParameterAblationEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_90MBPS_SIGNAL_PARAMETER_ABLATION");
}

bool includeBalanced2048CapacityProfile()
{
    return envFlagEnabled("SIRIUSSCOPE_INCLUDE_2048_CAPACITY_PROFILE");
}

std::vector<std::size_t> batchSweepMultipliers()
{
    return {1, 2, 4, 8};
}

std::vector<std::size_t> defaultProfileSelectionMultipliers()
{
    return {4, 8};
}

std::string trimAsciiWhitespace(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size()
           && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin
           && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::optional<CapacityProfile> parseCapacityProfile(const char* text)
{
    if (!text || *text == '\0') {
        return std::nullopt;
    }

    const auto value = trimAsciiWhitespace(text);
    if (value == "current") {
        return CapacityProfile::Current;
    }
    if (value == "balanced1024") {
        return CapacityProfile::Balanced1024;
    }
    if (value == "balanced2048") {
        return CapacityProfile::Balanced2048;
    }
    return std::nullopt;
}

CapacityProfile capacityProfileOrCurrent(const char* text)
{
    return parseCapacityProfile(text).value_or(CapacityProfile::Current);
}

CapacityProfile envCapacityProfileOrDefault()
{
    const char* value = std::getenv("SIRIUSSCOPE_90MBPS_CAPACITY_PROFILE");
    if (!value) {
        return CapacityProfile::Current;
    }

    const auto parsed = parseCapacityProfile(value);
    if (parsed) {
        return *parsed;
    }

    std::cout << "WARNING: invalid SIRIUSSCOPE_90MBPS_CAPACITY_PROFILE='"
              << value << "', using current\n";
    return CapacityProfile::Current;
}

std::vector<CapacityProfile> capacitySweepProfiles()
{
    std::vector<CapacityProfile> profiles{
        CapacityProfile::Current,
        CapacityProfile::Balanced1024,
    };
    if (includeBalanced2048CapacityProfile()) {
        profiles.push_back(CapacityProfile::Balanced2048);
    }
    return profiles;
}

std::optional<std::vector<std::size_t>> parseBatchMultiplierList(const char* text)
{
    if (!text || *text == '\0') {
        return std::nullopt;
    }

    std::vector<std::size_t> multipliers;
    const std::string value(text);
    std::size_t tokenBegin = 0;
    while (tokenBegin <= value.size()) {
        const auto tokenEnd = value.find(',', tokenBegin);
        const auto length =
            tokenEnd == std::string::npos ? std::string::npos : tokenEnd - tokenBegin;
        const auto token = trimAsciiWhitespace(value.substr(tokenBegin, length));
        if (token.empty()) {
            return std::nullopt;
        }

        const auto parsed = parseBatchMultiplier(token.c_str());
        if (!parsed) {
            return std::nullopt;
        }
        if (std::find(multipliers.begin(), multipliers.end(), *parsed)
            == multipliers.end()) {
            multipliers.push_back(*parsed);
        }

        if (tokenEnd == std::string::npos) {
            break;
        }
        tokenBegin = tokenEnd + 1;
    }

    if (multipliers.empty()) {
        return std::nullopt;
    }
    return multipliers;
}

std::vector<std::size_t> profileSelectionMultipliers()
{
    const char* value =
        std::getenv("SIRIUSSCOPE_90MBPS_PROFILE_SELECTION_MULTIPLIERS");
    if (!value) {
        return defaultProfileSelectionMultipliers();
    }

    const auto parsed = parseBatchMultiplierList(value);
    if (parsed) {
        return *parsed;
    }

    std::cout
        << "WARNING: invalid SIRIUSSCOPE_90MBPS_PROFILE_SELECTION_MULTIPLIERS='"
        << value << "', using 4,8\n";
    return defaultProfileSelectionMultipliers();
}

bool targetRawPipelineTestEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_90MBPS_PIPELINE_TEST");
}

bool baselineRaw60mbpsPipelineTestEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_BASELINE_60MBPS_PIPELINE_TEST");
}

bool strictTargetRawPipelineSustainRequired()
{
    return envFlagEnabled("SIRIUSSCOPE_REQUIRE_90MBPS_NO_DROPS");
}

bool detailedSpectrumTimingEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_ENABLE_DETAILED_SPECTRUM_TIMING");
}

bool detailedBearingTimingEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_ENABLE_DETAILED_BEARING_TIMING");
}

bool detailedSignalParameterTimingEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_ENABLE_DETAILED_SIGNAL_PARAMETER_TIMING");
}

bool parallelProcessingEngineEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_ENABLE_PARALLEL_PROCESSING_ENGINE");
}

bool targetRawSoakTestEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_90MBPS_SOAK_TEST");
}

std::chrono::seconds targetRawAuditDuration()
{
    return std::chrono::seconds{
        envPositiveIntOr("SIRIUSSCOPE_90MBPS_DURATION_SEC", 10),
    };
}

std::chrono::seconds baselineRaw60mbpsAuditDuration()
{
    return std::chrono::seconds{
        envPositiveIntOr("SIRIUSSCOPE_BASELINE_60MBPS_DURATION_SEC", 30),
    };
}

std::chrono::seconds targetRawSoakDuration()
{
    return std::chrono::seconds{
        envPositiveIntOr("SIRIUSSCOPE_90MBPS_SOAK_DURATION_SEC", 30),
    };
}

LatencyBudget targetRawLatencyBudget()
{
    return LatencyBudget{
        true,
        envPositiveDoubleOr("SIRIUSSCOPE_MAX_FANOUT_END_TO_END_MS", 8000.0),
        envPositiveDoubleOr("SIRIUSSCOPE_MAX_STAGE_QUEUE_WAIT_MS", 8000.0),
        envDepthRatioOr("SIRIUSSCOPE_MAX_STAGE_QUEUE_DEPTH_RATIO", 0.95),
    };
}

const char* auditModeName(AuditMode mode)
{
    switch (mode) {
    case AuditMode::Smoke:
        return "Smoke";
    case AuditMode::BaselineRaw60:
        return "BaselineRaw60";
    case AuditMode::TargetRawShort:
        return "TargetRawShort";
    case AuditMode::TargetRawStrict:
        return "TargetRawStrict";
    case AuditMode::TargetRawSoak:
        return "TargetRawSoak";
    }

    return "Unknown";
}

const char* processingModeName(pipeline::ProcessingMode mode)
{
    switch (mode) {
    case pipeline::ProcessingMode::Sequential:
        return "Sequential";
    case pipeline::ProcessingMode::ParallelFanOut:
        return "ParallelFanOut";
    }

    return "Unknown";
}

const char* stageOverloadPolicyName(pipeline::StageOverloadPolicy policy)
{
    switch (policy) {
    case pipeline::StageOverloadPolicy::LosslessRequired:
        return "lossless";
    case pipeline::StageOverloadPolicy::BoundedLatencyDropOldest:
        return "drop-oldest";
    case pipeline::StageOverloadPolicy::RealtimeLatestOnly:
        return "latest-only";
    }

    return "unknown";
}

const char* budgetStatusName(BudgetStatus status)
{
    switch (status) {
    case BudgetStatus::NotApplicable:
        return "N/A";
    case BudgetStatus::Pass:
        return "PASS";
    case BudgetStatus::Warn:
        return "WARN";
    case BudgetStatus::Fail:
        return "FAIL";
    }

    return "UNKNOWN";
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

std::vector<StageBacklog> activeParallelStageBacklogs(const AuditResult& result)
{
    std::vector<StageBacklog> stages{
        {"waterfall", result.pipeline.waterfallStage},
        {"spectrum", result.pipeline.spectrumStage},
        {"bearing", result.pipeline.bearingStage},
    };
    if (result.signalParameterStageEnabled) {
        stages.push_back({"signalParameter", result.pipeline.signalParameterStage});
    }
    return stages;
}

double queueDepthRatio(const pipeline::StageMetricsSnapshot& metrics)
{
    return ratioOrZero(static_cast<double>(metrics.queueMaxDepth),
                       static_cast<double>(metrics.queueCapacity));
}

double maxStageQueueDepthRatio(const AuditResult& result)
{
    double maxRatio = 0.0;
    for (const auto& stage : activeParallelStageBacklogs(result)) {
        maxRatio = std::max(maxRatio, queueDepthRatio(stage.metrics));
    }
    return maxRatio;
}

bool latencyBudgetIsHard(const AuditResult& result)
{
    return result.auditMode == AuditMode::TargetRawStrict
        || result.auditMode == AuditMode::TargetRawSoak;
}

BudgetStatus budgetStatus(bool enabled, bool applicable, bool violated, bool hard)
{
    if (!enabled || !applicable) {
        return BudgetStatus::NotApplicable;
    }
    if (violated) {
        return hard ? BudgetStatus::Fail : BudgetStatus::Warn;
    }
    return BudgetStatus::Pass;
}

LatencyBudgetEvaluation evaluateLatencyBudget(const AuditResult& result)
{
    LatencyBudgetEvaluation evaluation;
    const bool active = result.latencyBudget.enabled
        && result.processingMode == pipeline::ProcessingMode::ParallelFanOut;
    const bool hard = latencyBudgetIsHard(result);

    const bool fanOutApplicable =
        active && result.pipeline.parallelFanOutEndToEndLatency.count > 0;
    const bool fanOutViolated =
        fanOutApplicable
        && result.pipeline.parallelFanOutEndToEndLatency.maxMs
            > result.latencyBudget.maxFanOutEndToEndMs;
    evaluation.fanOutEndToEndStatus =
        budgetStatus(result.latencyBudget.enabled, fanOutApplicable, fanOutViolated, hard);

    for (const auto& stage : activeParallelStageBacklogs(result)) {
        const auto& metrics = stage.metrics;
        const bool queueWaitApplicable = active && metrics.queueWaitLatency.count > 0;
        const bool queueWaitViolated =
            queueWaitApplicable
            && metrics.queueWaitLatency.maxMs > result.latencyBudget.maxStageQueueWaitMs;
        const bool queueDepthApplicable = active && metrics.queueCapacity > 0;
        const auto depthRatio = queueDepthRatio(metrics);
        const bool queueDepthViolated =
            queueDepthApplicable
            && depthRatio > result.latencyBudget.maxStageQueueDepthRatio;

        evaluation.stages.push_back(StageBudgetEvaluation{
            stage.name,
            depthRatio,
            budgetStatus(result.latencyBudget.enabled,
                         queueWaitApplicable,
                         queueWaitViolated,
                         hard),
            budgetStatus(result.latencyBudget.enabled,
                         queueDepthApplicable,
                         queueDepthViolated,
                         hard),
        });
    }

    return evaluation;
}

BacklogSample makeBacklogSample(double elapsedSec,
                                const pipeline::PipelineMetricsSnapshot& metrics)
{
    return BacklogSample{
        elapsedSec,
        metrics.waterfallStageQueueDepth,
        metrics.spectrumStageQueueDepth,
        metrics.bearingStageQueueDepth,
        metrics.signalParameterStageQueueDepth,
        metrics.parallelFanOutInFlightBlocks,
        metrics.blockPoolInUse,
    };
}

void printQueueStability(const AuditResult& result)
{
    const auto evaluation = evaluateLatencyBudget(result);
    const auto stages = activeParallelStageBacklogs(result);

    std::cout << "Queue stability:\n"
              << "  latencyBudgetEnabled = "
              << (result.latencyBudget.enabled ? "true" : "false") << '\n'
              << "  latencyBudgetHard = "
              << (latencyBudgetIsHard(result) ? "true" : "false") << '\n'
              << "  fanOutEndToEnd max/budget/status = "
              << result.pipeline.parallelFanOutEndToEndLatency.maxMs << "/"
              << result.latencyBudget.maxFanOutEndToEndMs << "/"
              << budgetStatusName(evaluation.fanOutEndToEndStatus) << '\n'
              << "  maxStageQueueWait budget = "
              << result.latencyBudget.maxStageQueueWaitMs << '\n'
              << "  maxStageQueueDepthRatio budget = "
              << result.latencyBudget.maxStageQueueDepthRatio << '\n';

    for (std::size_t index = 0; index < stages.size(); ++index) {
        const auto& stage = stages[index];
        const auto& metrics = stage.metrics;
        const auto& stageEvaluation = evaluation.stages[index];
        std::cout << "  " << stage.name << " maxDepth/capacity/ratio/status = "
                  << metrics.queueMaxDepth << "/" << metrics.queueCapacity << "/"
                  << stageEvaluation.queueMaxDepthRatio << "/"
                  << budgetStatusName(stageEvaluation.queueDepthStatus) << '\n'
                  << "  " << stage.name << " queueWaitMaxMs/budget/status = "
                  << metrics.queueWaitLatency.maxMs << "/"
                  << result.latencyBudget.maxStageQueueWaitMs << "/"
                  << budgetStatusName(stageEvaluation.queueWaitStatus) << '\n';
    }
}

const char* backlogTrendName(BacklogTrend trend);
CapacityBacklogTrend makeCapacityBacklogTrend(const AuditResult& result);

void printBacklogTrend(const AuditResult& result)
{
    std::cout << "Backlog samples:\n";
    if (result.backlogSamples.empty()) {
        std::cout << "  no active samples\n";
        return;
    }

    std::size_t waterfallMax = 0;
    std::size_t spectrumMax = 0;
    std::size_t bearingMax = 0;
    std::size_t signalParameterMax = 0;
    std::uint64_t inFlightMax = 0;
    std::size_t blockPoolInUseMax = 0;

    for (const auto& sample : result.backlogSamples) {
        waterfallMax = std::max(waterfallMax, sample.waterfallDepth);
        spectrumMax = std::max(spectrumMax, sample.spectrumDepth);
        bearingMax = std::max(bearingMax, sample.bearingDepth);
        if (result.signalParameterStageEnabled) {
            signalParameterMax = std::max(signalParameterMax, sample.signalParameterDepth);
        }
        inFlightMax = std::max(inFlightMax, sample.inFlightFanOutBlocks);
        blockPoolInUseMax = std::max(blockPoolInUseMax, sample.blockPoolInUse);

        std::cout << "  t=" << sample.elapsedSec
                  << "s waterfallDepth=" << sample.waterfallDepth
                  << " spectrumDepth=" << sample.spectrumDepth
                  << " bearingDepth=" << sample.bearingDepth
                  << " signalParameterDepth=";
        if (result.signalParameterStageEnabled) {
            std::cout << sample.signalParameterDepth;
        } else {
            std::cout << "disabled";
        }
        std::cout
                  << " inFlightFanOutBlocks=" << sample.inFlightFanOutBlocks
                  << " blockPoolInUse=" << sample.blockPoolInUse << '\n';
    }

    const auto& first = result.backlogSamples.front();
    const auto& last = result.backlogSamples.back();
    std::cout << "Backlog trend:\n"
              << "  waterfall first/last/max/finalAfterFlush = "
              << first.waterfallDepth << "/" << last.waterfallDepth << "/"
              << waterfallMax << "/" << result.pipeline.waterfallStageQueueDepth << '\n'
              << "  spectrum first/last/max/finalAfterFlush = "
              << first.spectrumDepth << "/" << last.spectrumDepth << "/"
              << spectrumMax << "/" << result.pipeline.spectrumStageQueueDepth << '\n'
              << "  bearing first/last/max/finalAfterFlush = "
              << first.bearingDepth << "/" << last.bearingDepth << "/"
              << bearingMax << "/" << result.pipeline.bearingStageQueueDepth << '\n';
    std::cout << "  signalParameter first/last/max/finalAfterFlush = ";
    if (result.signalParameterStageEnabled) {
        std::cout << first.signalParameterDepth << "/" << last.signalParameterDepth << "/"
                  << signalParameterMax << "/"
                  << result.pipeline.signalParameterStageQueueDepth << '\n';
    } else {
        std::cout << "disabled\n";
    }
    std::cout << "  inFlight first/last/max/finalAfterFlush = "
              << first.inFlightFanOutBlocks << "/" << last.inFlightFanOutBlocks
              << "/" << inFlightMax << "/"
              << result.pipeline.parallelFanOutInFlightBlocks << '\n'
              << "  blockPoolInUse first/last/max/finalAfterFlush = "
              << first.blockPoolInUse << "/" << last.blockPoolInUse << "/"
              << blockPoolInUseMax << "/" << result.pipeline.blockPoolInUse
              << '\n';
    const auto trend = makeCapacityBacklogTrend(result);
    std::cout << "Backlog classification:\n"
              << "  waterfall = " << backlogTrendName(trend.waterfall) << '\n'
              << "  spectrum = " << backlogTrendName(trend.spectrum) << '\n'
              << "  bearing = " << backlogTrendName(trend.bearing) << '\n'
              << "  signalParameter = "
              << (result.signalParameterStageEnabled
                      ? backlogTrendName(trend.signalParameter)
                      : "disabled")
              << '\n'
              << "  overall = " << backlogTrendName(trend.overall) << '\n';
}

std::vector<LatencyStage> signalParameterInternalLatencies(
    const pipeline::PipelineMetricsSnapshot& metrics)
{
    return {
        {"ingest", metrics.signalParameterIngestLatency},
        {"sampleLoop", metrics.signalParameterSampleLoopLatency},
        {"bandLookup", metrics.signalParameterBandLookupLatency},
        {"pulseStateUpdate", metrics.signalParameterPulseStateUpdateLatency},
        {"spanUpdate", metrics.signalParameterSpanUpdateLatency},
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
    if (!detailedSpectrumTimingEnabled()) {
        std::cout << "SpectrumAggregator detailed timing: disabled\n"
                  << "SpectrumAggregator internal bottleneck by avg: n/a\n"
                  << "SpectrumAggregator internal bottleneck by max: n/a\n";
        return;
    }

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
              << " = " << byMax->stats.maxMs << " ms\n"
              << "Spectrum window mode: incremental active = "
              << (metrics.spectrumIncrementalWindowBlocks > 0 ? "true" : "false")
              << ", fallback count = "
              << metrics.spectrumIncrementalWindowFallbacks << '\n';
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
    if (!detailedSignalParameterTimingEnabled()) {
        std::cout << "SignalParameterAggregator detailed timing: disabled\n"
                  << "SignalParameterAggregator internal bottleneck by avg: n/a\n"
                  << "SignalParameterAggregator internal bottleneck by max: n/a\n";
        return;
    }

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

void printParallelFanOutBacklog(const AuditResult& result)
{
    std::cout << "Parallel fan-out backlog:\n";
    if (result.processingMode != pipeline::ProcessingMode::ParallelFanOut) {
        std::cout << "  not active\n";
        return;
    }

    const auto stages = activeParallelStageBacklogs(result);
    const auto durationSeconds = std::max(0.001, static_cast<double>(result.duration.count()));
    for (const auto& stage : stages) {
        const auto& metrics = stage.metrics;
        std::cout << "  " << stage.name
                  << " queue depth/max/capacity = " << metrics.queueDepth << "/"
                  << metrics.queueMaxDepth << "/" << metrics.queueCapacity << '\n'
                  << "  " << stage.name << " queueWait avg/max/count = "
                  << metrics.queueWaitLatency.averageMs() << "/"
                  << metrics.queueWaitLatency.maxMs << "/"
                  << metrics.queueWaitLatency.count << '\n'
                  << "  " << stage.name << " service avg/max/count = "
                  << metrics.serviceLatency.averageMs() << "/"
                  << metrics.serviceLatency.maxMs << "/"
                  << metrics.serviceLatency.count << '\n'
                  << "  " << stage.name << " enqueued/dequeued/processed/failures = "
                  << metrics.enqueuedBlocks << "/" << metrics.dequeuedBlocks << "/"
                  << metrics.processedBlocks << "/" << metrics.submitFailures << '\n'
                  << "  " << stage.name << " dropped/coalesced/skipped = "
                  << metrics.droppedByOverloadPolicy << "/"
                  << metrics.coalescedByOverloadPolicy << "/"
                  << metrics.skippedBlocks << '\n'
                  << "  " << stage.name << " processedBlocks/samples = "
                  << metrics.processedBlocks << "/" << metrics.processedSamples << '\n'
                  << "  " << stage.name << " throughput blocks/s samples/s = "
                  << static_cast<double>(metrics.processedBlocks) / durationSeconds << "/"
                  << static_cast<double>(metrics.processedSamples) / durationSeconds
                  << '\n';
    }
    if (!result.signalParameterStageEnabled) {
        std::cout << "  signalParameter disabled\n";
    }

    const auto byDepth =
        std::max_element(stages.begin(), stages.end(), [](const auto& left, const auto& right) {
            return left.metrics.queueMaxDepth < right.metrics.queueMaxDepth;
        });
    const auto byWait =
        std::max_element(stages.begin(), stages.end(), [](const auto& left, const auto& right) {
            return left.metrics.queueWaitLatency.averageMs()
                < right.metrics.queueWaitLatency.averageMs();
        });
    const auto byService =
        std::max_element(stages.begin(), stages.end(), [](const auto& left, const auto& right) {
            return left.metrics.serviceLatency.averageMs()
                < right.metrics.serviceLatency.averageMs();
        });

    std::cout << "  backlog hint by max queue depth: " << byDepth->name
              << " = " << byDepth->metrics.queueMaxDepth << '\n'
              << "  backlog hint by avg queue wait: " << byWait->name
              << " = " << byWait->metrics.queueWaitLatency.averageMs() << " ms\n"
              << "  slowest service stage by avg latency: " << byService->name
              << " = " << byService->metrics.serviceLatency.averageMs() << " ms\n";
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
    case hardware::SimulatorLoadProfile::BaselineRawThroughput60MBps: {
        const auto target = hardware::baselineRawThroughput60MbpsTarget();
        const auto samplesPerBatch = hardware::samplesPerBatchForTarget(target);
        const auto batchPeriodMs = std::max<std::int64_t>(1, target.batchPeriod.count());
        return std::max<std::size_t>(
            1,
            samplesPerBatch * 1000ULL / static_cast<std::uint64_t>(batchPeriodMs));
    }
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
    case hardware::SimulatorLoadProfile::BaselineRawThroughput60MBps:
        return "BaselineRawThroughput60MBps";
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

pipeline::StageOverloadConfig makeStageOverloadConfigFromEnv()
{
    pipeline::StageOverloadConfig config;
    if (!visualBackpressurePolicyEnabled()) {
        return config;
    }

    const auto visualPolicy = envVisualStagePolicyOrDefault();
    config.waterfall = visualPolicy;
    config.spectrum = visualPolicy;
    config.bearing = visualBearingBestEffortEnabled()
        ? visualPolicy
        : pipeline::StageOverloadPolicy::LosslessRequired;
    config.signalParameter = pipeline::StageOverloadPolicy::LosslessRequired;
    config.maxVisualQueueWait = envVisualMaxQueueWait();
    config.maxVisualQueueDepthRatio = envVisualMaxQueueDepthRatio();
    return config;
}

pipeline::DataIngestPipelineConfig makePipelineConfig(
    const core::TimeBase& timeBase,
    hardware::SimulatorLoadProfile profile,
    AuditPipelineSizing sizing = AuditPipelineSizing::Default,
    std::size_t batchSizeMultiplier = 1,
    CapacityProfile capacityProfile = CapacityProfile::Current,
    bool forceParallelProcessingEngine = false,
    bool enableSignalParameterStage = true)
{
    pipeline::DataIngestPipelineConfig config;
    const bool useParallelProcessing =
        forceParallelProcessingEngine || parallelProcessingEngineEnabled();
    config.blockPool = pipeline::SignalBlockPoolConfig{256, 20'000};
    config.queueCapacity = 256;
    if (profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps) {
        const auto maxSamplesPerBlock = targetRawMaxSamplesPerBlock(batchSizeMultiplier);
        if (sizing == AuditPipelineSizing::FullTargetRawSustain) {
            const bool useCapacityProfile = useParallelProcessing;
            const auto capacity =
                capacityProfileConfig(useCapacityProfile ? capacityProfile
                                                         : CapacityProfile::Current,
                                      batchSizeMultiplier);
            config.blockPool = pipeline::SignalBlockPoolConfig{
                capacity.blockPoolCapacity,
                maxSamplesPerBlock,
            };
            config.queueCapacity = capacity.pipelineQueueCapacity;
        } else {
            config.blockPool = pipeline::SignalBlockPoolConfig{4, maxSamplesPerBlock};
            config.queueCapacity = 2;
        }
    } else if (profile == hardware::SimulatorLoadProfile::BaselineRawThroughput60MBps) {
        config.blockPool = pipeline::SignalBlockPoolConfig{
            256,
            hardware::samplesPerBatchForTarget(
                hardware::baselineRawThroughput60MbpsTarget()),
        };
        config.queueCapacity = 128;
    }
    config.diagnosticsPublishInterval = std::chrono::milliseconds{1000};
    config.acceptingOnStart = true;

    config.waterfall.renderBinCount = kRenderBinCount;
    config.waterfall.sourceMinHz = kSourceMinHz;
    config.waterfall.sourceMaxHz = kSourceMaxHz;
    config.waterfall.rowPeriodNs = kRowPeriodNs;
    config.waterfall.timeBase = timeBase;
    const bool fullTargetRawSustain =
        profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps
        && sizing == AuditPipelineSizing::FullTargetRawSustain;
    const bool baselineRaw60 =
        profile == hardware::SimulatorLoadProfile::BaselineRawThroughput60MBps;
    config.waterfallRows = pipeline::WaterfallRowQueueConfig{
        fullTargetRawSustain || baselineRaw60 ? 16'384ULL : 4096ULL,
        pipeline::WaterfallOverflowPolicy::DropOldest,
    };

    config.spectrum.renderBinCount = kRenderBinCount;
    config.spectrum.sourceMinHz = kSourceMinHz;
    config.spectrum.sourceMaxHz = kSourceMaxHz;
    config.spectrum.snapshotPeriodNs = kRowPeriodNs;
    config.spectrum.timeBase = timeBase;
    config.spectrum.enableDetailedTiming = detailedSpectrumTimingEnabled();

    config.bearing.frequencyBinCount = kRenderBinCount;
    config.bearing.sourceMinHz = kSourceMinHz;
    config.bearing.sourceMaxHz = kSourceMaxHz;
    config.bearing.windowPeriodNs = kRowPeriodNs;
    config.bearing.fallbackAntennaAzimuthDeg = 45.0;
    config.bearing.timeBase = timeBase;
    config.bearing.enableDetailedTiming = detailedBearingTimingEnabled();

    config.signalParameters.estimatorConfig.samplePeriodNs = timeBase.samplePeriodNs;
    config.signalParameters.enableDetailedTiming = detailedSignalParameterTimingEnabled();
    config.processing.enableSignalParameterStage = enableSignalParameterStage;
    if (useParallelProcessing) {
        config.processing.processingMode = pipeline::ProcessingMode::ParallelFanOut;
        const bool useCapacityProfile =
            profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps
            && sizing == AuditPipelineSizing::FullTargetRawSustain;
        const auto capacity =
            capacityProfileConfig(useCapacityProfile ? capacityProfile
                                                     : CapacityProfile::Current,
                                  batchSizeMultiplier);
        config.processing.stageQueueCapacity =
            useCapacityProfile
            ? capacity.stageQueueCapacity
            : (baselineRaw60 ? 128 : 64);
    }
    config.processing.overloadPolicy = makeStageOverloadConfigFromEnv();
    return config;
}

pipeline::SourceToPipelineBridgeConfig makeBridgeConfig(
    hardware::SimulatorLoadProfile profile,
    AuditPipelineSizing sizing = AuditPipelineSizing::Default,
    std::size_t batchSizeMultiplier = 1,
    CapacityProfile capacityProfile = CapacityProfile::Current,
    bool forceParallelProcessingEngine = false)
{
    pipeline::SourceToPipelineBridgeConfig config;
    const bool useParallelProcessing =
        forceParallelProcessingEngine || parallelProcessingEngineEnabled();
    if (profile == hardware::SimulatorLoadProfile::BaselineRawThroughput60MBps) {
        config.queueCapacity = 32;
    } else if (profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps) {
        if (sizing == AuditPipelineSizing::FullTargetRawSustain
            && useParallelProcessing) {
            config.queueCapacity =
                capacityProfileConfig(capacityProfile, batchSizeMultiplier)
                    .bridgeQueueCapacity;
        } else {
            config.queueCapacity =
                sizing == AuditPipelineSizing::FullTargetRawSustain
                ? scaledCapacity(64, batchSizeMultiplier, 8)
                : 2;
        }
    } else {
        config.queueCapacity = 128;
    }
    config.overflowPolicy = pipeline::RxOverflowPolicy::DropNewest;
    return config;
}

hardware::SimulatorBcoLoadConfig makeLoadConfig(
    hardware::SimulatorLoadProfile profile,
    std::size_t batchSizeMultiplier = 1)
{
    hardware::SimulatorBcoLoadConfig config;
    config.profile = profile;
    config.batchPeriod = std::chrono::milliseconds{10};
    config.deterministic = true;
    config.minVisibleAmplitude = 1;
    if (profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps) {
        config.samplesPerBatchMultiplier =
            normalizeBatchMultiplier(batchSizeMultiplier);
    }
    return config;
}

AuditResult runAudit(std::chrono::seconds duration,
                     hardware::SimulatorLoadProfile profile,
                     std::chrono::milliseconds flushTimeout = std::chrono::milliseconds{5000},
                     std::optional<std::uint64_t> maxPipelineIngestedBlocks = std::nullopt,
                     AuditPipelineSizing sizing = AuditPipelineSizing::Default,
                     AuditMode auditMode = AuditMode::Smoke,
                     LatencyBudget latencyBudget = {},
                     std::size_t batchSizeMultiplier = 1,
                     CapacityProfile capacityProfile = CapacityProfile::Current,
                     bool forceParallelProcessingEngine = false,
                     std::optional<bool> signalParameterStageEnabledOverride = std::nullopt)
{
    AuditResult result;
    result.auditMode = auditMode;
    result.profileName = profileName(profile);
    result.duration = duration;
    result.latencyBudget = latencyBudget;
    result.signalParameterStageEnabled =
        signalParameterStageEnabledOverride.value_or(!signalParameterStageDisabledByEnv());
    result.batchSizeMultiplier = normalizeBatchMultiplier(batchSizeMultiplier);
    result.capacityProfile = capacityProfile;
    result.capacityProfileName = capacityProfileName(capacityProfile);
    result.capacityProfileApplied =
        profile == hardware::SimulatorLoadProfile::TargetRawThroughput90MBps
        && sizing == AuditPipelineSizing::FullTargetRawSustain
        && (forceParallelProcessingEngine || parallelProcessingEngineEnabled());

    const auto streamConfig = makeStreamConfig(profile);
    auto pipelineConfig = makePipelineConfig(streamConfig.timeBase,
                                             profile,
                                             sizing,
                                             result.batchSizeMultiplier,
                                             capacityProfile,
                                             forceParallelProcessingEngine,
                                             result.signalParameterStageEnabled);
    auto bridgeConfig = makeBridgeConfig(profile,
                                         sizing,
                                         result.batchSizeMultiplier,
                                         capacityProfile,
                                         forceParallelProcessingEngine);
    result.bridgeQueueCapacity = bridgeConfig.queueCapacity;
    result.pipelineQueueCapacity = pipelineConfig.queueCapacity;
    result.blockPoolCapacity = pipelineConfig.blockPool.blockCount;
    result.stageQueueCapacity = pipelineConfig.processing.stageQueueCapacity;
    result.samplesPerBlock = pipelineConfig.blockPool.maxSamplesPerBlock;
    result.estimatedBlockBytes = estimatedBlockBytes(result.samplesPerBlock);
    result.estimatedPoolBytes =
        estimatedPoolBytes(result.blockPoolCapacity, result.samplesPerBlock);
    result.processingMode = pipelineConfig.processing.processingMode;
    result.visualBackpressureEnabled = visualBackpressurePolicyEnabled();
    result.visualStagePolicy = result.visualBackpressureEnabled
        ? envVisualStagePolicyOrDefault()
        : pipeline::StageOverloadPolicy::LosslessRequired;
    result.visualMaxQueueWait =
        pipelineConfig.processing.overloadPolicy.maxVisualQueueWait;
    result.visualMaxQueueDepthRatio =
        pipelineConfig.processing.overloadPolicy.maxVisualQueueDepthRatio;
    result.visualBearingBestEffort =
        result.visualBackpressureEnabled && visualBearingBestEffortEnabled();
    result.allowVisualDegradationInStrict = visualDegradationAllowedInStrict();
    std::unique_ptr<pipeline::DataIngestPipeline> dataPipeline;
    try {
        dataPipeline = std::make_unique<pipeline::DataIngestPipeline>(
            std::move(pipelineConfig));
    } catch (const std::bad_alloc& exception) {
        result.profileSetupSucceeded = false;
        result.profileSetupError =
            std::string{"capacity profile setup failed: "} + exception.what();
        return result;
    } catch (const std::exception& exception) {
        result.profileSetupSucceeded = false;
        result.profileSetupError =
            std::string{"capacity profile setup failed: "} + exception.what();
        return result;
    }
    FixedAntennaAzimuthProvider antenna(45.0);
    hardware::HighLoadSimulatorBcoStreamSource source(
        makeLoadConfig(profile, result.batchSizeMultiplier),
        nullptr,
        &antenna);

    const auto configured = source.configure(streamConfig);
    result.sourceConfigured = configured.success;
    if (!configured) {
        return result;
    }

    const auto pipelineStarted = dataPipeline->start();
    result.pipelineStarted = pipelineStarted.success;
    if (!pipelineStarted) {
        return result;
    }

    pipeline::SourceToPipelineBridge bridge(dataPipeline.get(), std::move(bridgeConfig));
    const auto bridgeStarted = bridge.start();
    result.bridgeStarted = bridgeStarted.success;
    if (!bridgeStarted) {
        dataPipeline->stop();
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
        const auto startedAt = std::chrono::steady_clock::now();
        const auto stopAt = startedAt + duration;
        auto nextSampleAt = startedAt + std::chrono::seconds{1};

        while (std::chrono::steady_clock::now() < stopAt) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextSampleAt) {
                const auto elapsedSec =
                    std::chrono::duration<double>(now - startedAt).count();
                result.backlogSamples.push_back(
                    makeBacklogSample(elapsedSec, dataPipeline->metricsSnapshot()));
                nextSampleAt += std::chrono::seconds{1};
                continue;
            }

            const auto remaining = stopAt - now;
            const auto untilNextSample = nextSampleAt - now;
            auto sleepDuration = std::min(remaining, untilNextSample);
            sleepDuration = std::min(
                sleepDuration,
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::milliseconds{100}));
            if (sleepDuration > std::chrono::steady_clock::duration::zero()) {
                std::this_thread::sleep_for(sleepDuration);
            }
        }

        const auto elapsedSec =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt)
                .count();
        result.backlogSamples.push_back(
            makeBacklogSample(elapsedSec, dataPipeline->metricsSnapshot()));
    }

    const auto sourceStopped = source.stop();
    result.sourceStopped = sourceStopped.success;

    const auto bridgeFlushed = bridge.flush(flushTimeout);
    result.bridgeFlushed = bridgeFlushed.success;
    bridge.stop();

    const auto flushed = dataPipeline->flushProcessing(flushTimeout);
    result.flushed = flushed.success;

    const auto drainedRows = dataPipeline->drainWaterfallRows(1'000'000);
    result.drainedWaterfallRows = drainedRows.size();
    result.source = source.metrics();
    result.bridge = bridge.metrics();
    result.pipeline = dataPipeline->metricsSnapshot();
    result.waterfallRows = dataPipeline->waterfallRowQueueMetrics();
    result.rejectedBlocks = result.bridge.rejectedBlocks;
    result.rejectedSamples = result.bridge.rejectedSamples;
    result.hasSpectrumSnapshot = dataPipeline->latestSpectrumSnapshot() != nullptr;
    result.hasBearingSnapshot = dataPipeline->latestBearingSnapshot() != nullptr;
    result.hasSignalParameterSnapshot =
        dataPipeline->latestSignalParameterSnapshot() != nullptr;

    if (!result.flushed) {
        dataPipeline->clearQueuedBlocks();
    }
    dataPipeline->stop();
    return result;
}

bool criticalNoDropPasses(const AuditResult& result);
std::uint64_t visualStageSkippedBlocks(const AuditResult& result);
bool visualDegradationActive(const AuditResult& result);
BudgetStatus visualDegradationStatus(const AuditResult& result);
const char* passFailName(bool pass);

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
              << "  auditMode = " << auditModeName(result.auditMode) << '\n'
              << "  profile = " << result.profileName << '\n'
              << "  durationSec = " << result.duration.count() << '\n'
              << "  strictNoDropMode = "
              << (strictTargetRawPipelineSustainRequired() ? "true" : "false")
              << '\n'
              << "  latencyBudgetEnabled = "
              << (result.latencyBudget.enabled ? "true" : "false") << '\n'
              << "  processingMode = " << processingModeName(result.processingMode)
              << '\n'
              << "  signalParameterStageEnabled = "
              << (result.signalParameterStageEnabled ? "true" : "false") << '\n'
              << "  batchSizeMultiplier = " << result.batchSizeMultiplier << '\n'
              << "  capacityProfile = " << result.capacityProfileName << '\n'
              << "  capacityProfileApplied = "
              << (result.capacityProfileApplied ? "true" : "false") << '\n'
              << "  profileSetupSucceeded = "
              << (result.profileSetupSucceeded ? "true" : "false") << '\n';
    if (!result.profileSetupError.empty()) {
        std::cout << "  profileSetupError = " << result.profileSetupError << '\n';
    }
    std::cout << "  bridgeQueueCapacity = " << result.bridgeQueueCapacity << '\n'
              << "  pipelineQueueCapacity = " << result.pipelineQueueCapacity << '\n'
              << "  blockPoolCapacityConfigured = "
              << result.blockPoolCapacity << '\n'
              << "  stageQueueCapacity = " << result.stageQueueCapacity << '\n'
              << "  samplesPerBlock = " << result.samplesPerBlock << '\n'
              << "  estimatedBlockBytes = " << result.estimatedBlockBytes << '\n'
              << "  estimatedPoolBytes = " << result.estimatedPoolBytes << '\n'
              << "Overload policy:\n"
              << "  visualBackpressureEnabled = "
              << (result.visualBackpressureEnabled ? "true" : "false") << '\n'
              << "  visualStagePolicy = "
              << stageOverloadPolicyName(result.visualStagePolicy) << '\n'
              << "  visualMaxQueueWaitMs = "
              << result.visualMaxQueueWait.count() << '\n'
              << "  visualMaxQueueDepthRatio = "
              << result.visualMaxQueueDepthRatio << '\n'
              << "  visualBearingBestEffort = "
              << (result.visualBearingBestEffort ? "true" : "false") << '\n'
              << "  allowVisualDegradationInStrict = "
              << (result.allowVisualDegradationInStrict ? "true" : "false")
              << '\n'
              << "  source producedSamples = " << result.source.producedSamples << '\n'
              << "  source producedBatches = " << result.source.producedBatches << '\n'
              << "  source producedSamplesPerBatchAvg = "
              << producedSamplesPerBatch(result) << '\n'
              << "  source producedBatchesPerSecond = "
              << producedBatchesPerSecond(result) << '\n'
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
              << "  parallelFanOutBlocks = "
              << result.pipeline.parallelFanOutBlocks << '\n'
              << "  fanOutBlocksPerSecond = "
              << parallelFanOutBlocksPerSecond(result) << '\n'
              << "  parallelFanOutFallbackBlocks = "
              << result.pipeline.parallelFanOutFallbackBlocks << '\n'
              << "  parallelFanOutRejectedBlocks = "
              << result.pipeline.parallelFanOutRejectedBlocks << '\n'
              << "  parallelFanOutInFlightBlocks = "
              << result.pipeline.parallelFanOutInFlightBlocks << '\n'
              << "  waterfallStageQueueDepth = "
              << result.pipeline.waterfallStageQueueDepth << '\n'
              << "  spectrumStageQueueDepth = "
              << result.pipeline.spectrumStageQueueDepth << '\n'
              << "  bearingStageQueueDepth = "
              << result.pipeline.bearingStageQueueDepth << '\n'
              << "  signalParameterStageQueueDepth = "
              << result.pipeline.signalParameterStageQueueDepth << '\n'
              << "  waterfallStageProcessedBlocks = "
              << result.pipeline.waterfallStageProcessedBlocks << '\n'
              << "  waterfallStageBlocksPerSecond = "
              << stageBlocksPerSecond(result, result.pipeline.waterfallStage) << '\n'
              << "  spectrumStageProcessedBlocks = "
              << result.pipeline.spectrumStageProcessedBlocks << '\n'
              << "  spectrumStageBlocksPerSecond = "
              << stageBlocksPerSecond(result, result.pipeline.spectrumStage) << '\n'
              << "  bearingStageProcessedBlocks = "
              << result.pipeline.bearingStageProcessedBlocks << '\n'
              << "  bearingStageBlocksPerSecond = "
              << stageBlocksPerSecond(result, result.pipeline.bearingStage) << '\n'
              << "  signalParameterStageProcessedBlocks = "
              << result.pipeline.signalParameterStageProcessedBlocks << '\n'
              << "  signalParameterStageBlocksPerSecond = ";
    if (result.signalParameterStageEnabled) {
        std::cout << stageBlocksPerSecond(result, result.pipeline.signalParameterStage)
                  << '\n';
    } else {
        std::cout << "disabled\n";
    }
    std::cout << "Visual stage policy results:\n"
              << "  waterfall dropped/coalesced/skipped = "
              << result.pipeline.waterfallStageDroppedByPolicy << "/"
              << result.pipeline.waterfallStageCoalescedByPolicy << "/"
              << result.pipeline.waterfallStageSkippedBlocks << '\n'
              << "  spectrum dropped/coalesced/skipped = "
              << result.pipeline.spectrumStageDroppedByPolicy << "/"
              << result.pipeline.spectrumStageCoalescedByPolicy << "/"
              << result.pipeline.spectrumStageSkippedBlocks << '\n'
              << "  bearing dropped/coalesced/skipped = "
              << result.pipeline.bearingStageDroppedByPolicy << "/"
              << result.pipeline.bearingStageCoalescedByPolicy << "/"
              << result.pipeline.bearingStageSkippedBlocks << '\n'
              << "  signalParameter dropped/coalesced/skipped = "
              << result.pipeline.signalParameterStageDroppedByPolicy << "/"
              << result.pipeline.signalParameterStageCoalescedByPolicy << "/"
              << result.pipeline.signalParameterStageSkippedBlocks << '\n'
              << "Critical lossless status:\n"
              << "  bridge rejectedBlocks = " << result.bridge.rejectedBlocks << '\n'
              << "  blockPoolExhausted = "
              << result.pipeline.blockPoolExhausted << '\n'
              << "  input/processed = " << result.pipeline.inputSamples << "/"
              << result.pipeline.processedSamples << '\n'
              << "  signalParameter policy drops = "
              << (result.signalParameterStageEnabled
                      ? std::to_string(result.pipeline.signalParameterStageDroppedByPolicy)
                      : std::string{"disabled"})
              << '\n'
              << "  criticalNoDrop = "
              << passFailName(criticalNoDropPasses(result)) << '\n'
              << "  visualDegradation = "
              << budgetStatusName(visualDegradationStatus(result)) << '\n'
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
              << "  parallelFanOutEndToEndMs avg/max = "
              << result.pipeline.parallelFanOutEndToEndLatency.averageMs() << "/"
              << result.pipeline.parallelFanOutEndToEndLatency.maxMs << '\n'
              << "  waterfallMs avg/max = "
              << result.pipeline.waterfallAggregationLatency.averageMs() << "/"
              << result.pipeline.waterfallAggregationLatency.maxMs << '\n'
              << "  spectrumMs avg/max = "
              << result.pipeline.spectrumAggregationLatency.averageMs() << "/"
              << result.pipeline.spectrumAggregationLatency.maxMs << '\n'
              << "  bearingMs avg/max = "
              << result.pipeline.bearingAggregationLatency.averageMs() << "/"
              << result.pipeline.bearingAggregationLatency.maxMs << '\n'
              << "  signalParametersMs avg/max = ";
    if (result.signalParameterStageEnabled) {
        std::cout << result.pipeline.signalParameterAggregationLatency.averageMs() << "/"
                  << result.pipeline.signalParameterAggregationLatency.maxMs << '\n';
    } else {
        std::cout << "disabled\n";
    }
    std::cout
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
              << "  Spectrum detailed timing: "
              << (detailedSpectrumTimingEnabled() ? "enabled" : "disabled") << '\n'
              << "  detailedSpectrumTimingEnabled = "
              << (detailedSpectrumTimingEnabled() ? "true" : "false") << '\n'
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
              << "  usedIncrementalWindowIndex = "
              << (result.pipeline.spectrumIncrementalWindowBlocks > 0 ? "true"
                                                                       : "false")
              << '\n'
              << "  spectrumFastWindowBlocks = "
              << result.pipeline.spectrumFastWindowBlocks << '\n'
              << "  spectrumFastBinBlocks = "
              << result.pipeline.spectrumFastBinBlocks << '\n'
              << "  spectrumFastBandSummaryBlocks = "
              << result.pipeline.spectrumFastBandSummaryBlocks << '\n'
              << "  spectrumIncrementalWindowBlocks = "
              << result.pipeline.spectrumIncrementalWindowBlocks << '\n'
              << "  spectrumIncrementalWindowFallbacks = "
              << result.pipeline.spectrumIncrementalWindowFallbacks << '\n'
              << "  spectrumBlockLocalAccumulationBlocks = "
              << result.pipeline.spectrumBlockLocalAccumulationBlocks << '\n'
              << "Bearing micro-breakdown:\n"
              << "  Bearing detailed timing: "
              << (detailedBearingTimingEnabled() ? "enabled" : "disabled") << '\n'
              << "  detailedBearingTimingEnabled = "
              << (detailedBearingTimingEnabled() ? "true" : "false") << '\n'
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
              << "  bearingBlockLocalAccumulationBlocks = "
              << result.pipeline.bearingBlockLocalAccumulationBlocks << '\n'
              << "SignalParameter critical diagnostics:\n"
              << "  stage = "
              << (result.signalParameterStageEnabled ? "enabled" : "disabled")
              << '\n'
              << "  detailedTiming = "
              << (detailedSignalParameterTimingEnabled() ? "enabled" : "disabled")
              << '\n'
              << "  trustedFixedBandFastPathBlocks = "
              << result.pipeline.signalParameterTrustedFixedBandFastPathBlocks << '\n'
              << "  blockLocalFastPathBlocks = "
              << result.pipeline.signalParameterBlockLocalFastPathBlocks << '\n'
              << "  inputSamples = "
              << result.pipeline.signalParameterInputSamples << '\n'
              << "  acceptedSamples = "
              << result.pipeline.signalParameterAcceptedSamples << '\n'
              << "  rejectedSamples = "
              << result.pipeline.signalParameterRejectedSamples << '\n'
              << "  touchedBandsTotal = "
              << result.pipeline.signalParameterTouchedBands << '\n'
              << "  belowThresholdFastSkips = "
              << result.pipeline.signalParameterBelowThresholdFastSkips << '\n'
              << "  pulseTransitions = "
              << result.pipeline.signalParameterPulseTransitions << '\n'
              << "  activePulseUpdates = "
              << result.pipeline.signalParameterActivePulseUpdates << '\n'
              << "  completedPulses = "
              << result.pipeline.signalParameterCompletedPulses << '\n'
              << "  outOfOrderSamples = "
              << result.pipeline.signalParameterOutOfOrderSamples << '\n'
              << "Signal parameter micro-breakdown:\n";
    if (!result.signalParameterStageEnabled) {
        std::cout << "  stage disabled\n";
    }
    std::cout << "  ingest avg/max = "
              << (result.signalParameterStageEnabled
                      ? std::to_string(result.pipeline.signalParameterIngestLatency.averageMs())
                      : std::string{"disabled"});
    if (result.signalParameterStageEnabled) {
        std::cout << "/" << result.pipeline.signalParameterIngestLatency.maxMs;
    }
    std::cout << '\n'
              << "  SignalParameter detailed timing: "
              << (detailedSignalParameterTimingEnabled() ? "enabled" : "disabled")
              << '\n'
              << "  sampleLoop avg/max = "
              << result.pipeline.signalParameterSampleLoopLatency.averageMs() << "/"
              << result.pipeline.signalParameterSampleLoopLatency.maxMs << '\n'
              << "  bandLookup avg/max = "
              << result.pipeline.signalParameterBandLookupLatency.averageMs() << "/"
              << result.pipeline.signalParameterBandLookupLatency.maxMs << '\n'
              << "  pulseStateUpdate avg/max = "
              << result.pipeline.signalParameterPulseStateUpdateLatency.averageMs() << "/"
              << result.pipeline.signalParameterPulseStateUpdateLatency.maxMs << '\n'
              << "  spanUpdate avg/max = "
              << result.pipeline.signalParameterSpanUpdateLatency.averageMs() << "/"
              << result.pipeline.signalParameterSpanUpdateLatency.maxMs << '\n'
              << (detailedSignalParameterTimingEnabled()
                      ? ""
                      : "  detailed sub-stage timings disabled\n")
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
    printQueueStability(result);
    printBacklogTrend(result);
    printParallelFanOutBacklog(result);
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
    if (result.pipeline.parallelFanOutFallbackBlocks > 0) {
        printSustainWarning("parallel fan-out fell back to sequential blocks: "
                            + std::to_string(
                                result.pipeline.parallelFanOutFallbackBlocks));
    }
    if (visualDegradationActive(result)) {
        printSustainWarning("visual stage degradation active: dropped="
                            + std::to_string(result.pipeline.visualStageDroppedBlocks)
                            + " coalesced="
                            + std::to_string(result.pipeline.visualStageCoalescedBlocks)
                            + " skipped="
                            + std::to_string(visualStageSkippedBlocks(result)));
    }
    const auto budgetEvaluation = evaluateLatencyBudget(result);
    if (!latencyBudgetIsHard(result) && budgetEvaluation.hasWarnings()) {
        printSustainWarning("latency/backlog budget exceeded in report-only mode");
    }
}

void assertLatencyBudgetSucceeded(TestRunner& test, const AuditResult& result)
{
    if (!result.latencyBudget.enabled || !latencyBudgetIsHard(result)) {
        return;
    }

    const auto evaluation = evaluateLatencyBudget(result);
    test.require(evaluation.fanOutEndToEndStatus != BudgetStatus::Fail,
                 "strict/soak fan-out end-to-end latency budget passes");
    for (const auto& stage : evaluation.stages) {
        test.require(stage.queueWaitStatus != BudgetStatus::Fail,
                     std::string{"strict/soak "} + stage.name
                         + " queue wait latency budget passes");
        test.require(stage.queueDepthStatus != BudgetStatus::Fail,
                     std::string{"strict/soak "} + stage.name
                         + " queue depth budget passes");
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
    if (result.signalParameterStageEnabled) {
        test.require(result.hasSignalParameterSnapshot,
                     "pipeline publishes signal parameter snapshot");
        test.require(result.pipeline.producedSignalParameterSnapshots > 0,
                     "pipeline metrics count produced signal parameter snapshots");
    } else {
        test.require(!result.hasSignalParameterSnapshot,
                     "disabled signal parameter stage publishes no snapshot");
        test.require(result.pipeline.producedSignalParameterSnapshots == 0,
                     "disabled signal parameter stage produces no snapshot metrics");
    }
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
    if (result.signalParameterStageEnabled) {
        test.require(result.hasSignalParameterSnapshot,
                     "target raw full pipeline publishes signal parameter snapshot");
        test.require(result.pipeline.producedSignalParameterSnapshots > 0,
                     "target raw full pipeline metrics count signal parameter snapshots");
    } else {
        test.require(!result.hasSignalParameterSnapshot,
                     "target raw disabled signal parameter publishes no snapshot");
        test.require(result.pipeline.producedSignalParameterSnapshots == 0,
                     "target raw disabled signal parameter produces no snapshot metrics");
    }
    if (result.pipeline.producedBearingSnapshots > 0) {
        test.require(result.hasBearingSnapshot,
                     "target raw full pipeline publishes bearing snapshot");
    } else {
        printSustainWarning("bearing snapshot assertion skipped because no bearing "
                            "snapshots were produced");
    }

    assertLatencyBudgetSucceeded(test, result);

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
    test.require(result.pipeline.parallelFanOutFallbackBlocks == 0,
                 "strict target raw full pipeline has no fan-out fallback blocks");
    test.require(result.pipeline.parallelFanOutRejectedBlocks == 0,
                 "strict target raw full pipeline has no fan-out rejected blocks");
    test.require(result.pipeline.parallelFanOutInFlightBlocks == 0,
                 "strict target raw full pipeline has no fan-out blocks in flight after flush");
    test.require(result.pipeline.blockPoolInUse == 0,
                 "strict target raw full pipeline returns all blocks to the pool after flush");
    if (result.signalParameterStageEnabled) {
        test.require(result.pipeline.signalParameterStageDroppedByPolicy == 0,
                     "strict target raw full pipeline keeps signal parameter policy drops at zero");
        test.require(result.pipeline.signalParameterStageCoalescedByPolicy == 0,
                     "strict target raw full pipeline keeps signal parameter policy coalesces at zero");
    }
    test.require(!visualDegradationActive(result)
                     || result.allowVisualDegradationInStrict,
                 "strict target raw full pipeline allows visual degradation only by explicit env");
    test.require(result.source.simulatorBackpressureEvents == 0,
                 "strict target raw full pipeline source reports no backpressure events");
    const double rawThroughputRelativeError =
        std::abs(result.source.producedRawBytesPerSecond
                 - static_cast<double>(result.source.targetBytesPerSecond))
        / static_cast<double>(result.source.targetBytesPerSecond);
    test.require(rawThroughputRelativeError <= 0.05,
                 "strict target raw full pipeline raw throughput stays within 5 percent");
}

void printBaselineRaw60Summary(const AuditResult& result)
{
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Baseline 60 MB/s summary\n\n"
              << "mode " << processingModeName(result.processingMode) << '\n'
              << "rawMBps " << result.source.producedRawBytesPerSecond / 1'000'000.0 << '\n'
              << "expectedRawMBps "
              << static_cast<double>(kExpectedBaselineRawBytesPerSecond) / 1'000'000.0
              << '\n'
              << "inputSamples " << result.pipeline.inputSamples << '\n'
              << "processedSamples " << result.pipeline.processedSamples << '\n'
              << "processed/input " << ratioOrZero(
                     static_cast<double>(result.pipeline.processedSamples),
                     static_cast<double>(result.pipeline.inputSamples)) << '\n'
              << "rejectedBlocks " << result.bridge.rejectedBlocks << '\n'
              << "droppedBlocks "
              << (result.bridge.droppedBlocks + result.pipeline.droppedBlocks) << '\n'
              << "queueDroppedBlocks " << result.pipeline.queueDroppedBlocks << '\n'
              << "blockPoolExhausted " << result.pipeline.blockPoolExhausted << '\n'
              << "fanOutAvgMs "
              << result.pipeline.parallelFanOutEndToEndLatency.averageMs() << '\n'
              << "fanOutMaxMs "
              << result.pipeline.parallelFanOutEndToEndLatency.maxMs << '\n'
              << "maxQueueRatio " << maxStageQueueDepthRatio(result) << '\n'
              << "waterfallQmax " << result.pipeline.waterfallStage.queueMaxDepth << '\n'
              << "spectrumQmax " << result.pipeline.spectrumStage.queueMaxDepth << '\n'
              << "bearingQmax " << result.pipeline.bearingStage.queueMaxDepth << '\n'
              << "signalParameterStage "
              << (result.signalParameterStageEnabled ? "enabled" : "disabled") << '\n';
}

bool baselineRaw60PipelineAcceptancePasses(const AuditResult& result)
{
    const double expectedRelativeError =
        std::abs(result.source.producedRawBytesPerSecond
                 - static_cast<double>(kExpectedBaselineRawBytesPerSecond))
        / static_cast<double>(kExpectedBaselineRawBytesPerSecond);
    const double targetRelativeError =
        std::abs(result.source.producedRawBytesPerSecond
                 - static_cast<double>(kBaselineRawBytesPerSecond))
        / static_cast<double>(kBaselineRawBytesPerSecond);
    const bool bearingSnapshotOk =
        result.pipeline.producedBearingSnapshots == 0 || result.hasBearingSnapshot;

    return result.sourceConfigured && result.pipelineStarted && result.bridgeStarted
        && result.sourceStarted && result.sourceStopped && result.bridgeFlushed
        && result.flushed
        && result.source.targetBytesPerSecond == kBaselineRawBytesPerSecond
        && result.source.producedSamples > 0
        && (expectedRelativeError <= 0.05 || targetRelativeError <= 0.05)
        && result.bridge.droppedBlocks == 0
        && result.bridge.rejectedBlocks == 0
        && result.pipeline.droppedBlocks == 0
        && result.pipeline.droppedSamples == 0
        && result.pipeline.queueDroppedBlocks == 0
        && result.pipeline.blockPoolExhausted == 0
        && result.pipeline.processedSamples == result.pipeline.inputSamples
        && result.pipeline.parallelFanOutRejectedBlocks == 0
        && result.pipeline.parallelFanOutFallbackBlocks == 0
        && result.pipeline.parallelFanOutInFlightBlocks == 0
        && result.pipeline.blockPoolInUse == 0
        && result.hasSpectrumSnapshot
        && bearingSnapshotOk
        && !result.hasSignalParameterSnapshot
        && result.pipeline.producedSignalParameterSnapshots == 0
        && result.pipeline.signalParameterStageProcessedBlocks == 0;
}

void assertBaselineRaw60PipelineSustain(TestRunner& test, const AuditResult& result)
{
    test.require(result.sourceConfigured,
                 "baseline raw source accepts stream config");
    test.require(result.pipelineStarted,
                 "baseline data ingest pipeline starts");
    test.require(result.bridgeStarted,
                 "baseline source-to-pipeline bridge starts");
    test.require(result.sourceStarted,
                 "baseline high-load source starts");
    test.require(result.sourceStopped,
                 "baseline high-load source stops");
    test.require(result.bridgeFlushed,
                 "baseline source-to-pipeline bridge flushes");
    test.require(result.flushed,
                 "baseline data ingest pipeline flushes");

    test.require(result.source.targetBytesPerSecond == kBaselineRawBytesPerSecond,
                 "baseline source reports configured raw byte target");
    test.require(result.source.producedSamples > 0,
                 "baseline source produces samples");
    test.require(result.source.producedRawBytes > 0,
                 "baseline source reports produced raw bytes");

    const double expectedRelativeError =
        std::abs(result.source.producedRawBytesPerSecond
                 - static_cast<double>(kExpectedBaselineRawBytesPerSecond))
        / static_cast<double>(kExpectedBaselineRawBytesPerSecond);
    const double targetRelativeError =
        std::abs(result.source.producedRawBytesPerSecond
                 - static_cast<double>(kBaselineRawBytesPerSecond))
        / static_cast<double>(kBaselineRawBytesPerSecond);
    test.require(expectedRelativeError <= 0.05 || targetRelativeError <= 0.05,
                 "baseline raw throughput stays within 5 percent");

    test.require(result.bridge.receivedBlocks > 0,
                 "baseline bridge receives source blocks");
    test.require(result.bridge.droppedBlocks == 0,
                 "baseline bridge reports no dropped blocks");
    test.require(result.bridge.rejectedBlocks == 0,
                 "baseline bridge reports no rejected blocks");
    test.require(result.pipeline.droppedBlocks == 0,
                 "baseline pipeline reports no dropped blocks");
    test.require(result.pipeline.droppedSamples == 0,
                 "baseline pipeline reports no dropped samples");
    test.require(result.pipeline.queueDroppedBlocks == 0,
                 "baseline bounded queue reports no dropped blocks");
    test.require(result.pipeline.blockPoolExhausted == 0,
                 "baseline block pool is not exhausted");
    test.require(result.pipeline.processedSamples == result.pipeline.inputSamples,
                 "baseline pipeline processes every accepted sample");
    test.require(result.pipeline.parallelFanOutRejectedBlocks == 0,
                 "baseline has no fan-out rejected blocks");
    test.require(result.pipeline.parallelFanOutFallbackBlocks == 0,
                 "baseline has no fan-out fallback blocks");
    test.require(result.pipeline.parallelFanOutInFlightBlocks == 0,
                 "baseline has no fan-out blocks in flight after flush");
    test.require(result.pipeline.blockPoolInUse == 0,
                 "baseline returns all blocks to the pool after flush");
    test.require(result.hasSpectrumSnapshot,
                 "baseline publishes spectrum snapshot");
    if (result.pipeline.producedBearingSnapshots > 0) {
        test.require(result.hasBearingSnapshot,
                     "baseline publishes bearing snapshot when bearing snapshots are produced");
    }
    test.require(!result.hasSignalParameterSnapshot,
                 "baseline publishes no signal parameter snapshot");
    test.require(result.pipeline.producedSignalParameterSnapshots == 0,
                 "baseline metrics count no signal parameter snapshots");
    test.require(result.pipeline.signalParameterStageProcessedBlocks == 0,
                 "baseline sends no blocks to signal parameter stage");
}

double rawMegabytesPerSecond(const AuditResult& result)
{
    return result.source.producedRawBytesPerSecond / 1'000'000.0;
}

double processedToInputRatio(const AuditResult& result)
{
    return ratioOrZero(static_cast<double>(result.pipeline.processedSamples),
                       static_cast<double>(result.pipeline.inputSamples));
}

bool visualDegradationActive(const AuditResult& result);

const char* passFailName(bool pass)
{
    return pass ? "PASS" : "FAIL";
}

BudgetStatus visualDegradationStatus(const AuditResult& result)
{
    if (!visualDegradationActive(result)) {
        return BudgetStatus::Pass;
    }
    if (strictTargetRawPipelineSustainRequired()
        && !result.allowVisualDegradationInStrict) {
        return BudgetStatus::Fail;
    }
    return BudgetStatus::Warn;
}

bool budgetStatusPasses(BudgetStatus status)
{
    return status == BudgetStatus::Pass || status == BudgetStatus::NotApplicable;
}

bool latencyBudgetPasses(const LatencyBudgetEvaluation& evaluation)
{
    if (!budgetStatusPasses(evaluation.fanOutEndToEndStatus)) {
        return false;
    }

    return std::all_of(evaluation.stages.begin(),
                       evaluation.stages.end(),
                       [](const auto& stage) {
                           return budgetStatusPasses(stage.queueWaitStatus);
                       });
}

bool queueDepthBudgetPasses(const LatencyBudgetEvaluation& evaluation)
{
    return std::all_of(evaluation.stages.begin(),
                       evaluation.stages.end(),
                       [](const auto& stage) {
                           return budgetStatusPasses(stage.queueDepthStatus);
                       });
}

BudgetStatus worstBudgetStatus(BudgetStatus current, BudgetStatus next)
{
    if (current == BudgetStatus::Fail || next == BudgetStatus::Fail) {
        return BudgetStatus::Fail;
    }
    if (current == BudgetStatus::Warn || next == BudgetStatus::Warn) {
        return BudgetStatus::Warn;
    }
    if (current == BudgetStatus::Pass || next == BudgetStatus::Pass) {
        return BudgetStatus::Pass;
    }
    return BudgetStatus::NotApplicable;
}

BudgetStatus combinedLatencyBacklogStatus(const LatencyBudgetEvaluation& evaluation)
{
    auto status = evaluation.fanOutEndToEndStatus;
    for (const auto& stage : evaluation.stages) {
        status = worstBudgetStatus(status, stage.queueWaitStatus);
        status = worstBudgetStatus(status, stage.queueDepthStatus);
    }
    return status;
}

const char* backlogTrendName(BacklogTrend trend)
{
    switch (trend) {
    case BacklogTrend::Stable:
        return "Stable";
    case BacklogTrend::Growing:
        return "Growing";
    case BacklogTrend::Saturating:
        return "Saturating";
    case BacklogTrend::Unknown:
        return "Unknown";
    }

    return "Unknown";
}

int backlogTrendRank(BacklogTrend trend)
{
    switch (trend) {
    case BacklogTrend::Stable:
        return 0;
    case BacklogTrend::Growing:
        return 1;
    case BacklogTrend::Saturating:
        return 2;
    case BacklogTrend::Unknown:
        return 3;
    }

    return 3;
}

BacklogTrend worstBacklogTrend(BacklogTrend current, BacklogTrend next)
{
    return backlogTrendRank(next) > backlogTrendRank(current) ? next : current;
}

BacklogTrend classifyBacklogTrend(std::size_t firstDepth,
                                  std::size_t lastDepth,
                                  std::size_t capacity,
                                  double maxQueueRatio,
                                  std::size_t sampleCount)
{
    if (sampleCount < 2 || capacity == 0) {
        return BacklogTrend::Unknown;
    }
    if (maxQueueRatio >= 0.95) {
        return BacklogTrend::Saturating;
    }

    const auto growthThreshold = std::max<std::size_t>(2, capacity / 10);
    if (lastDepth > firstDepth + growthThreshold
        && static_cast<double>(lastDepth) > static_cast<double>(capacity) * 0.75) {
        return BacklogTrend::Growing;
    }

    return BacklogTrend::Stable;
}

CapacityBacklogTrend makeCapacityBacklogTrend(const AuditResult& result)
{
    CapacityBacklogTrend trend;
    if (result.backlogSamples.empty()) {
        return trend;
    }

    const auto& first = result.backlogSamples.front();
    const auto& last = result.backlogSamples.back();
    const auto sampleCount = result.backlogSamples.size();
    trend.waterfall = classifyBacklogTrend(first.waterfallDepth,
                                           last.waterfallDepth,
                                           result.pipeline.waterfallStage.queueCapacity,
                                           queueDepthRatio(result.pipeline.waterfallStage),
                                           sampleCount);
    trend.spectrum = classifyBacklogTrend(first.spectrumDepth,
                                          last.spectrumDepth,
                                          result.pipeline.spectrumStage.queueCapacity,
                                          queueDepthRatio(result.pipeline.spectrumStage),
                                          sampleCount);
    trend.bearing = classifyBacklogTrend(first.bearingDepth,
                                         last.bearingDepth,
                                         result.pipeline.bearingStage.queueCapacity,
                                         queueDepthRatio(result.pipeline.bearingStage),
                                         sampleCount);
    if (result.signalParameterStageEnabled) {
        trend.signalParameter =
            classifyBacklogTrend(first.signalParameterDepth,
                                 last.signalParameterDepth,
                                 result.pipeline.signalParameterStage.queueCapacity,
                                 queueDepthRatio(result.pipeline.signalParameterStage),
                                 sampleCount);
    }
    trend.overall = worstBacklogTrend(trend.waterfall, trend.spectrum);
    trend.overall = worstBacklogTrend(trend.overall, trend.bearing);
    if (result.signalParameterStageEnabled) {
        trend.overall = worstBacklogTrend(trend.overall, trend.signalParameter);
    }
    return trend;
}

bool batchProfileBudgetPasses(const BatchProfileScore& score)
{
    return score.latencyBudgetPass && score.queueDepthBudgetPass;
}

bool auditInfrastructureSucceeded(const AuditResult& result);

std::uint64_t visualStageSkippedBlocks(const AuditResult& result)
{
    return result.pipeline.waterfallStage.skippedBlocks
        + result.pipeline.spectrumStage.skippedBlocks
        + result.pipeline.bearingStage.skippedBlocks;
}

bool visualDegradationActive(const AuditResult& result)
{
    return result.pipeline.visualStageDroppedBlocks > 0
        || result.pipeline.visualStageCoalescedBlocks > 0
        || visualStageSkippedBlocks(result) > 0;
}

bool criticalNoDropPasses(const AuditResult& result)
{
    return auditInfrastructureSucceeded(result)
        && result.profileSetupSucceeded
        && result.flushed
        && result.pipeline.inputSamples > 0
        && result.pipeline.processedSamples > 0
        && result.bridge.droppedBlocks == 0
        && result.bridge.droppedSamples == 0
        && result.bridge.rejectedBlocks == 0
        && result.bridge.rejectedSamples == 0
        && result.pipeline.droppedBlocks == 0
        && result.pipeline.droppedSamples == 0
        && result.pipeline.queueDroppedBlocks == 0
        && result.pipeline.blockPoolExhausted == 0
        && result.pipeline.processedSamples == result.pipeline.inputSamples
        && result.pipeline.parallelFanOutFallbackBlocks == 0
        && result.pipeline.parallelFanOutRejectedBlocks == 0
        && result.pipeline.parallelFanOutInFlightBlocks == 0
        && result.pipeline.blockPoolInUse == 0
        && (!result.signalParameterStageEnabled
            || (result.pipeline.signalParameterStageDroppedByPolicy == 0
                && result.pipeline.signalParameterStageCoalescedByPolicy == 0))
        && result.source.simulatorBackpressureEvents == 0;
}

bool noDropPasses(const AuditResult& result)
{
    return criticalNoDropPasses(result)
        && (!strictTargetRawPipelineSustainRequired()
            || !visualDegradationActive(result)
            || result.allowVisualDegradationInStrict);
}

std::uint64_t profileDroppedBlocks(const AuditResult& result)
{
    return result.bridge.droppedBlocks + result.pipeline.droppedBlocks
        + result.pipeline.queueDroppedBlocks;
}

std::uint64_t profileDroppedSamples(const AuditResult& result)
{
    return result.bridge.droppedSamples + result.pipeline.droppedSamples;
}

double maxStageQueueWaitAverageMs(const AuditResult& result)
{
    double maxAverageMs = 0.0;
    for (const auto& stage : activeParallelStageBacklogs(result)) {
        maxAverageMs =
            std::max(maxAverageMs, stage.metrics.queueWaitLatency.averageMs());
    }
    return maxAverageMs;
}

double maxStageQueueWaitMaxMs(const AuditResult& result)
{
    double maxMs = 0.0;
    for (const auto& stage : activeParallelStageBacklogs(result)) {
        maxMs = std::max(maxMs, stage.metrics.queueWaitLatency.maxMs);
    }
    return maxMs;
}

double maxStageServiceMaxMs(const AuditResult& result)
{
    double maxMs = 0.0;
    for (const auto& stage : activeParallelStageBacklogs(result)) {
        maxMs = std::max(maxMs, stage.metrics.serviceLatency.maxMs);
    }
    return maxMs;
}

bool auditInfrastructureSucceeded(const AuditResult& result)
{
    return result.sourceConfigured && result.pipelineStarted && result.bridgeStarted
        && result.sourceStarted && result.sourceStopped && result.bridgeFlushed;
}

BatchProfileScore makeBatchProfileScore(const AuditResult& result)
{
    const auto evaluation = evaluateLatencyBudget(result);
    BatchProfileScore score;
    score.multiplier = result.batchSizeMultiplier;
    score.latencyBudgetPass = latencyBudgetPasses(evaluation);
    score.queueDepthBudgetPass = queueDepthBudgetPasses(evaluation);
    score.rawMBps = rawMegabytesPerSecond(result);
    score.fanOutMaxMs = result.pipeline.parallelFanOutEndToEndLatency.maxMs;
    score.maxQueueDepthRatio = maxStageQueueDepthRatio(result);
    score.rejectedBlocks = result.rejectedBlocks;
    score.blockPoolExhausted = result.pipeline.blockPoolExhausted;
    score.inputSamples = result.pipeline.inputSamples;
    score.processedSamples = result.pipeline.processedSamples;
    score.droppedBlocks = profileDroppedBlocks(result);
    score.droppedSamples = profileDroppedSamples(result);
    score.queueDroppedBlocks = result.pipeline.queueDroppedBlocks;
    score.noDropPass = noDropPasses(result);
    return score;
}

std::vector<BatchProfileScore> makeBatchProfileScores(
    const std::vector<AuditResult>& results)
{
    std::vector<BatchProfileScore> scores;
    scores.reserve(results.size());
    for (const auto& result : results) {
        scores.push_back(makeBatchProfileScore(result));
    }
    return scores;
}

bool shouldPreferBatchProfile(const BatchProfileScore& candidate,
                              const BatchProfileScore& current)
{
    constexpr double kFanOutCloseToleranceMs = 1.0;
    const bool candidateBudgetPasses = batchProfileBudgetPasses(candidate);
    const bool currentBudgetPasses = batchProfileBudgetPasses(current);
    if (candidateBudgetPasses != currentBudgetPasses) {
        return candidateBudgetPasses;
    }

    const auto fanOutDelta = std::abs(candidate.fanOutMaxMs - current.fanOutMaxMs);
    if (fanOutDelta > kFanOutCloseToleranceMs) {
        return candidate.fanOutMaxMs < current.fanOutMaxMs;
    }

    return candidate.multiplier < current.multiplier;
}

BatchProfileSelection selectRecommendedBatchProfile(
    const std::vector<BatchProfileScore>& scores)
{
    BatchProfileSelection selection;
    for (const auto& score : scores) {
        if (!score.noDropPass) {
            continue;
        }
        if (!selection.selected
            || shouldPreferBatchProfile(score, *selection.selected)) {
            selection.selected = score;
        }
    }

    if (!selection.selected) {
        selection.reason =
            "No production profile selected; continue optimization or capacity tuning.";
        return selection;
    }

    selection.reason = batchProfileBudgetPasses(*selection.selected)
        ? "selected no-drop profile that passes latency/backlog budget"
        : "selected no-drop profile with lowest fanOutEndToEnd max; "
          "latency/backlog budget is not fully passing";
    return selection;
}

bool capacityProfileBudgetPasses(const CapacityProfileScore& score)
{
    return score.latencyBudgetPass && score.queueDepthBudgetPass;
}

CapacityProfileScore makeCapacityProfileScore(const AuditResult& result)
{
    const auto evaluation = evaluateLatencyBudget(result);
    CapacityProfileScore score;
    score.profile = result.capacityProfile;
    score.profileName = result.capacityProfileName;
    score.noDropPass = noDropPasses(result);
    score.latencyBudgetPass = latencyBudgetPasses(evaluation);
    score.queueDepthBudgetPass = queueDepthBudgetPasses(evaluation);
    score.rawMBps = rawMegabytesPerSecond(result);
    score.fanOutMaxMs = result.pipeline.parallelFanOutEndToEndLatency.maxMs;
    score.maxQueueDepthRatio = maxStageQueueDepthRatio(result);
    score.rejectedBlocks = result.rejectedBlocks;
    score.blockPoolExhausted = result.pipeline.blockPoolExhausted;
    score.inputSamples = result.pipeline.inputSamples;
    score.processedSamples = result.pipeline.processedSamples;
    score.droppedBlocks = profileDroppedBlocks(result);
    score.droppedSamples = profileDroppedSamples(result);
    score.queueDroppedBlocks = result.pipeline.queueDroppedBlocks;
    score.backlogTrend = makeCapacityBacklogTrend(result);
    return score;
}

std::vector<CapacityProfileScore> makeCapacityProfileScores(
    const std::vector<AuditResult>& results)
{
    std::vector<CapacityProfileScore> scores;
    scores.reserve(results.size());
    for (const auto& result : results) {
        scores.push_back(makeCapacityProfileScore(result));
    }
    return scores;
}

bool shouldPreferCapacityProfile(const CapacityProfileScore& candidate,
                                 const CapacityProfileScore& current)
{
    const bool candidateBudgetPasses = capacityProfileBudgetPasses(candidate);
    const bool currentBudgetPasses = capacityProfileBudgetPasses(current);
    if (candidateBudgetPasses != currentBudgetPasses) {
        return candidateBudgetPasses;
    }

    const bool candidateLowQueue = candidate.maxQueueDepthRatio < 0.80;
    const bool currentLowQueue = current.maxQueueDepthRatio < 0.80;
    if (candidateLowQueue != currentLowQueue) {
        return candidateLowQueue;
    }

    const auto candidateTrendRank = backlogTrendRank(candidate.backlogTrend.overall);
    const auto currentTrendRank = backlogTrendRank(current.backlogTrend.overall);
    if (candidateTrendRank != currentTrendRank) {
        return candidateTrendRank < currentTrendRank;
    }

    return capacityProfileRank(candidate.profile) < capacityProfileRank(current.profile);
}

CapacityProfileSelection selectRecommendedCapacityProfile(
    const std::vector<CapacityProfileScore>& scores)
{
    CapacityProfileSelection selection;
    for (const auto& score : scores) {
        if (!score.noDropPass) {
            continue;
        }
        if (!selection.selected
            || shouldPreferCapacityProfile(score, *selection.selected)) {
            selection.selected = score;
        }
    }

    if (!selection.selected) {
        selection.reason =
            "No capacity profile selected; continue service-latency optimization or latency policy.";
        return selection;
    }

    selection.reason = "selected smallest no-drop capacity profile after budget, queue ratio, and backlog trend checks";
    if (selection.selected->backlogTrend.overall != BacklogTrend::Stable) {
        selection.reason +=
            "; selected only as short-duration guidance because backlog trend is not stable";
    }
    return selection;
}

bool rawThroughputValid(const AuditResult& result)
{
    if (result.source.targetBytesPerSecond == 0
        || result.source.producedRawBytesPerSecond <= 0.0) {
        return false;
    }

    const double relativeError =
        std::abs(result.source.producedRawBytesPerSecond
                 - static_cast<double>(result.source.targetBytesPerSecond))
        / static_cast<double>(result.source.targetBytesPerSecond);
    return relativeError <= 0.15;
}

void assertBatchSweepRunUsable(TestRunner& test, const AuditResult& result)
{
    test.require(auditInfrastructureSucceeded(result),
                 "batch sweep audit infrastructure succeeds");
    test.require(result.source.targetBytesPerSecond == kTargetRawBytesPerSecond,
                 "batch sweep source reports configured raw byte target");
    test.require(result.source.producedBatches > 0,
                 "batch sweep source produces batches");
    test.require(result.source.producedSamples > 0,
                 "batch sweep source produces samples");
    test.require(rawThroughputValid(result),
                 "batch sweep raw throughput stays within report tolerance");
}

void printBatchSweepSummary(const std::vector<AuditResult>& results)
{
    if (results.empty()) {
        return;
    }

    std::cout << "Batch multiplier sweep summary:\n"
              << "  m  rawMBps  blocks/s  samples/block  rejected  poolExh  processed/input\n";
    for (const auto& result : results) {
        std::cout << "  " << result.batchSizeMultiplier << "  "
                  << rawMegabytesPerSecond(result) << "  "
                  << producedBatchesPerSecond(result) << "  "
                  << producedSamplesPerBatch(result) << "  "
                  << result.rejectedBlocks << "  "
                  << result.pipeline.blockPoolExhausted << "  "
                  << processedToInputRatio(result) << '\n';
    }

    std::cout << "Backlog:\n"
              << "  m  fanoutAvg  fanoutMax  spectrumQmax  signalQmax  maxQueueRatio\n";
    for (const auto& result : results) {
        std::cout << "  " << result.batchSizeMultiplier << "  "
                  << result.pipeline.parallelFanOutEndToEndLatency.averageMs() << "  "
                  << result.pipeline.parallelFanOutEndToEndLatency.maxMs << "  "
                  << result.pipeline.spectrumStage.queueMaxDepth << "  "
                  << result.pipeline.signalParameterStage.queueMaxDepth << "  "
                  << maxStageQueueDepthRatio(result) << '\n';
    }

    std::cout << "Service:\n"
              << "  m  waterfallAvg  spectrumAvg  bearingAvg  signalAvg\n";
    for (const auto& result : results) {
        std::cout << "  " << result.batchSizeMultiplier << "  "
                  << result.pipeline.waterfallStage.serviceLatency.averageMs() << "  "
                  << result.pipeline.spectrumStage.serviceLatency.averageMs() << "  "
                  << result.pipeline.bearingStage.serviceLatency.averageMs() << "  "
                  << result.pipeline.signalParameterStage.serviceLatency.averageMs() << '\n';
    }
}

void printBatchProfileSelectionSummary(
    const std::vector<AuditResult>& results,
    const std::vector<BatchProfileScore>& scores,
    const BatchProfileSelection& selection)
{
    if (results.empty() || scores.empty()) {
        return;
    }

    const auto& first = results.front();
    std::cout << "Batch profile selection summary:\n"
              << "  strictMode = "
              << (strictTargetRawPipelineSustainRequired() ? "true" : "false")
              << '\n'
              << "  soakMode = "
              << (first.auditMode == AuditMode::TargetRawSoak ? "true" : "false")
              << '\n'
              << "  durationSec = " << first.duration.count() << '\n'
              << "  m  rawMBps  noDrop  budget  rejected  poolExh  dropped  "
                 "input/processed  inputMBps  processedMBps  blockPoolUsage\n";

    for (std::size_t index = 0; index < results.size() && index < scores.size();
         ++index) {
        const auto& result = results[index];
        const auto& score = scores[index];
        const auto budgetStatus =
            combinedLatencyBacklogStatus(evaluateLatencyBudget(result));
        std::cout << "  " << score.multiplier << "  "
                  << score.rawMBps << "  "
                  << passFailName(score.noDropPass) << "  "
                  << budgetStatusName(budgetStatus) << "  "
                  << score.rejectedBlocks << "  "
                  << score.blockPoolExhausted << "  "
                  << score.droppedBlocks << "  "
                  << score.inputSamples << "/" << score.processedSamples << "  "
                  << result.pipeline.inputMegabytesPerSecond << "  "
                  << result.pipeline.processedMegabytesPerSecond << "  "
                  << result.pipeline.blockPoolUsage << '\n';
    }

    std::cout << "Latency/backlog:\n"
              << "  m  fanoutAvg  fanoutMax  queueWaitAvg  queueWaitMax  "
                 "maxQueueRatio  spectrumQmax  signalQmax\n";
    for (const auto& result : results) {
        std::cout << "  " << result.batchSizeMultiplier << "  "
                  << result.pipeline.parallelFanOutEndToEndLatency.averageMs()
                  << "  "
                  << result.pipeline.parallelFanOutEndToEndLatency.maxMs
                  << "  "
                  << maxStageQueueWaitAverageMs(result) << "  "
                  << maxStageQueueWaitMaxMs(result) << "  "
                  << maxStageQueueDepthRatio(result) << "  "
                  << result.pipeline.spectrumStage.queueMaxDepth << "  "
                  << result.pipeline.signalParameterStage.queueMaxDepth << '\n';
    }

    std::cout
        << "Service:\n"
        << "  m  waterfallAvg/Max  spectrumAvg/Max  bearingAvg/Max  "
           "signalAvg/Max  maxStageServiceMax\n";
    for (const auto& result : results) {
        std::cout << "  " << result.batchSizeMultiplier << "  "
                  << result.pipeline.waterfallStage.serviceLatency.averageMs()
                  << "/" << result.pipeline.waterfallStage.serviceLatency.maxMs
                  << "  "
                  << result.pipeline.spectrumStage.serviceLatency.averageMs()
                  << "/" << result.pipeline.spectrumStage.serviceLatency.maxMs
                  << "  "
                  << result.pipeline.bearingStage.serviceLatency.averageMs()
                  << "/" << result.pipeline.bearingStage.serviceLatency.maxMs
                  << "  "
                  << result.pipeline.signalParameterStage.serviceLatency.averageMs()
                  << "/" << result.pipeline.signalParameterStage.serviceLatency.maxMs
                  << "  "
                  << maxStageServiceMaxMs(result) << '\n';
    }

    std::cout << "Recommendation:\n";
    if (selection.selected) {
        std::cout << "  selectedMultiplier = "
                  << selection.selected->multiplier << '\n';
    } else {
        std::cout << "  selectedMultiplier = none\n";
    }
    std::cout << "  reason = " << selection.reason << '\n';
}

void printCapacityProfileSweepSummary(
    const std::vector<AuditResult>& results,
    const std::vector<CapacityProfileScore>& scores,
    const CapacityProfileSelection& selection)
{
    if (results.empty() || scores.empty()) {
        return;
    }

    const auto& first = results.front();
    std::cout << "Capacity profile sweep summary, m=8:\n"
              << "  strictMode = "
              << (strictTargetRawPipelineSustainRequired() ? "true" : "false")
              << '\n'
              << "  soakMode = "
              << (first.auditMode == AuditMode::TargetRawSoak ? "true" : "false")
              << '\n'
              << "  durationSec = " << first.duration.count() << '\n'
              << "  profile  rawMBps  noDrop  budget  rejected  poolExh  dropped  "
                 "input/processed  fanoutMax  maxQueueRatio  trend\n";

    for (std::size_t index = 0; index < results.size() && index < scores.size();
         ++index) {
        const auto& result = results[index];
        const auto& score = scores[index];
        const auto budgetStatus =
            combinedLatencyBacklogStatus(evaluateLatencyBudget(result));
        std::cout << "  " << score.profileName << "  "
                  << score.rawMBps << "  "
                  << passFailName(score.noDropPass) << "  "
                  << budgetStatusName(budgetStatus) << "  "
                  << score.rejectedBlocks << "  "
                  << score.blockPoolExhausted << "  "
                  << score.droppedBlocks << "  "
                  << score.inputSamples << "/" << score.processedSamples << "  "
                  << score.fanOutMaxMs << "  "
                  << score.maxQueueDepthRatio << "  "
                  << backlogTrendName(score.backlogTrend.overall) << '\n';
    }

    std::cout << "Capacities:\n"
              << "  profile  bridgeQueue  pipelineQueue  blockPool  stageQueue  "
                 "samplesPerBlock  estimatedBlockBytes  estimatedPoolBytes\n";
    for (const auto& result : results) {
        std::cout << "  " << result.capacityProfileName << "  "
                  << result.bridgeQueueCapacity << "  "
                  << result.pipelineQueueCapacity << "  "
                  << result.blockPoolCapacity << "  "
                  << result.stageQueueCapacity << "  "
                  << result.samplesPerBlock << "  "
                  << result.estimatedBlockBytes << "  "
                  << result.estimatedPoolBytes << '\n';
    }

    std::cout << "Queues:\n"
              << "  profile  waterfallMax  spectrumMax  bearingMax  signalMax\n";
    for (const auto& result : results) {
        std::cout << "  " << result.capacityProfileName << "  "
                  << result.pipeline.waterfallStage.queueMaxDepth << "  "
                  << result.pipeline.spectrumStage.queueMaxDepth << "  "
                  << result.pipeline.bearingStage.queueMaxDepth << "  "
                  << result.pipeline.signalParameterStage.queueMaxDepth << '\n';
    }

    std::cout << "Queue ratios:\n"
              << "  profile  waterfallRatio  spectrumRatio  bearingRatio  "
                 "signalRatio\n";
    for (const auto& result : results) {
        std::cout << "  " << result.capacityProfileName << "  "
                  << queueDepthRatio(result.pipeline.waterfallStage) << "  "
                  << queueDepthRatio(result.pipeline.spectrumStage) << "  "
                  << queueDepthRatio(result.pipeline.bearingStage) << "  "
                  << queueDepthRatio(result.pipeline.signalParameterStage) << '\n';
    }

    std::cout << "Backlog trend:\n"
              << "  profile  waterfall  spectrum  bearing  signalParameter  overall\n";
    for (const auto& score : scores) {
        std::cout << "  " << score.profileName << "  "
                  << backlogTrendName(score.backlogTrend.waterfall) << "  "
                  << backlogTrendName(score.backlogTrend.spectrum) << "  "
                  << backlogTrendName(score.backlogTrend.bearing) << "  "
                  << backlogTrendName(score.backlogTrend.signalParameter) << "  "
                  << backlogTrendName(score.backlogTrend.overall) << '\n';
    }

    std::cout << "Recommendation:\n";
    if (selection.selected) {
        std::cout << "  selectedCapacityProfile = "
                  << selection.selected->profileName << '\n';
    } else {
        std::cout << "  selectedCapacityProfile = none\n";
    }
    std::cout << "  reason = " << selection.reason << '\n';
}

const char* signalParameterAblationConclusion(bool baselinePass, bool ablationPass)
{
    if (!baselinePass && ablationPass) {
        return "CONCLUSION: SignalParameterAggregator is the primary 90 MB/s bottleneck.";
    }
    if (baselinePass && ablationPass) {
        return "CONCLUSION: 90 MB/s passes with and without SignalParameterAggregator; bottleneck is not reproduced in this run.";
    }
    if (!baselinePass && !ablationPass) {
        return "CONCLUSION: Disabling SignalParameterAggregator is not sufficient; another bottleneck remains.";
    }
    return "CONCLUSION: Unexpected result; inspect run stability and test configuration.";
}

void printSignalParameterValueOrDisabled(const AuditResult& result, double value)
{
    if (result.signalParameterStageEnabled) {
        std::cout << value;
    } else {
        std::cout << "disabled";
    }
}

void printSignalParameterValueOrDisabled(const AuditResult& result, std::uint64_t value)
{
    if (result.signalParameterStageEnabled) {
        std::cout << value;
    } else {
        std::cout << "disabled";
    }
}

void printSignalParameterAblationRow(const char* mode, const AuditResult& result)
{
    std::cout << "  " << mode << "  "
              << rawMegabytesPerSecond(result) << "  "
              << passFailName(noDropPasses(result)) << "  "
              << result.pipeline.inputSamples << "  "
              << result.pipeline.processedSamples << "  "
              << processedToInputRatio(result) << "  "
              << result.rejectedBlocks << "  "
              << profileDroppedBlocks(result) << "  "
              << result.pipeline.queueDroppedBlocks << "  "
              << result.pipeline.blockPoolExhausted << "  "
              << result.pipeline.parallelFanOutEndToEndLatency.averageMs() << "  "
              << result.pipeline.parallelFanOutEndToEndLatency.maxMs << "  "
              << maxStageQueueDepthRatio(result) << "  "
              << result.pipeline.waterfallStage.queueMaxDepth << "  "
              << result.pipeline.spectrumStage.queueMaxDepth << "  "
              << result.pipeline.bearingStage.queueMaxDepth << "  ";
    printSignalParameterValueOrDisabled(
        result,
        static_cast<std::uint64_t>(result.pipeline.signalParameterStage.queueMaxDepth));
    std::cout << "  ";
    printSignalParameterValueOrDisabled(
        result,
        result.pipeline.signalParameterStage.serviceLatency.averageMs());
    std::cout << "  ";
    printSignalParameterValueOrDisabled(
        result,
        result.pipeline.signalParameterStage.serviceLatency.maxMs);
    std::cout << '\n';
}

void printSignalParameterAblationSummary(const AuditResult& baseline,
                                         const AuditResult& ablation)
{
    const bool baselinePass = noDropPasses(baseline);
    const bool ablationPass = noDropPasses(ablation);
    std::cout << "SignalParameter ablation summary\n"
              << "  mode  rawMBps  noDrop  inputSamples  processedSamples  "
                 "processed/input  rejectedBlocks  droppedBlocks  "
                 "queueDroppedBlocks  blockPoolExhausted  fanOutAvgMs  "
                 "fanOutMaxMs  maxQueueRatio  waterfallQmax  spectrumQmax  "
                 "bearingQmax  signalParameterQmax  signalParameterAvgMs  "
                 "signalParameterMaxMs\n";
    printSignalParameterAblationRow("baseline", baseline);
    printSignalParameterAblationRow("without-signal-parameters", ablation);
    std::cout << signalParameterAblationConclusion(baselinePass, ablationPass) << '\n';
}

bool stressTestsEnabled()
{
    return envFlagEnabled("SIRIUSSCOPE_RUN_STRESS_TESTS");
}

void testAuditHelperParsingAndBudget(TestRunner& test)
{
    test.require(parsePositiveInt("30").value_or(0) == 30,
                 "positive integer env value is parsed");
    test.require(!parsePositiveInt("0"),
                 "zero integer env value is rejected");
    test.require(!parsePositiveInt("invalid"),
                 "invalid integer env value is rejected");
    test.require(parseBatchMultiplier("1").value_or(0) == 1,
                 "batch multiplier accepts 1");
    test.require(parseBatchMultiplier("2").value_or(0) == 2,
                 "batch multiplier accepts 2");
    test.require(parseBatchMultiplier("4").value_or(0) == 4,
                 "batch multiplier accepts 4");
    test.require(parseBatchMultiplier("8").value_or(0) == 8,
                 "batch multiplier accepts 8");
    test.require(!parseBatchMultiplier("0"),
                 "batch multiplier rejects 0");
    test.require(!parseBatchMultiplier("3"),
                 "batch multiplier rejects unsupported values");
    test.require(!parseBatchMultiplier("-1"),
                 "batch multiplier rejects negative values");
    test.require(!parseBatchMultiplier("abc"),
                 "batch multiplier rejects non-numeric values");
    test.require(parseCapacityProfile("current").value_or(CapacityProfile::Balanced2048)
                     == CapacityProfile::Current,
                 "capacity profile accepts current");
    test.require(parseCapacityProfile("balanced1024")
                     .value_or(CapacityProfile::Current)
                     == CapacityProfile::Balanced1024,
                 "capacity profile accepts balanced1024");
    test.require(parseCapacityProfile("balanced2048")
                     .value_or(CapacityProfile::Current)
                     == CapacityProfile::Balanced2048,
                 "capacity profile accepts balanced2048");
    test.require(!parseCapacityProfile("oversized"),
                 "capacity profile rejects unsupported values");
    test.require(capacityProfileOrCurrent("oversized") == CapacityProfile::Current,
                 "invalid capacity profile falls back to current");
    const auto currentCapacity = capacityProfileConfig(CapacityProfile::Current, 8);
    test.require(currentCapacity.bridgeQueueCapacity == 8
                     && currentCapacity.pipelineQueueCapacity == 64
                     && currentCapacity.blockPoolCapacity == 64
                     && currentCapacity.stageQueueCapacity == 64,
                 "current capacity profile preserves m=8 computed sizing");
    const auto balanced1024 = capacityProfileConfig(CapacityProfile::Balanced1024, 8);
    test.require(balanced1024.bridgeQueueCapacity == 128
                     && balanced1024.pipelineQueueCapacity == 1024
                     && balanced1024.blockPoolCapacity == 1024
                     && balanced1024.stageQueueCapacity == 1024,
                 "balanced1024 capacity profile returns configured values");
    const auto balanced2048 = capacityProfileConfig(CapacityProfile::Balanced2048, 8);
    test.require(balanced2048.bridgeQueueCapacity == 256
                     && balanced2048.pipelineQueueCapacity == 2048
                     && balanced2048.blockPoolCapacity == 2048
                     && balanced2048.stageQueueCapacity == 2048,
                 "balanced2048 capacity profile returns configured values");
    test.require(classifyBacklogTrend(0, 0, 100, 0.0, 1)
                     == BacklogTrend::Unknown,
                 "backlog trend is unknown with insufficient samples");
    test.require(classifyBacklogTrend(0, 1, 100, 0.95, 2)
                     == BacklogTrend::Saturating,
                 "backlog trend saturates at 95 percent max ratio");
    test.require(classifyBacklogTrend(0, 80, 100, 0.80, 2)
                     == BacklogTrend::Growing,
                 "backlog trend detects growing high final depth");
    test.require(classifyBacklogTrend(5, 6, 100, 0.10, 2)
                     == BacklogTrend::Stable,
                 "backlog trend detects stable low queue");
    const auto sweepMultipliers = batchSweepMultipliers();
    test.require(sweepMultipliers == std::vector<std::size_t>({1, 2, 4, 8}),
                 "batch sweep values are 1, 2, 4, 8");
    test.require(defaultProfileSelectionMultipliers()
                     == std::vector<std::size_t>({4, 8}),
                 "profile selection defaults to multipliers 4 and 8");
    test.require(parseBatchMultiplierList("4,8").value_or(std::vector<std::size_t>{})
                     == std::vector<std::size_t>({4, 8}),
                 "profile multiplier list accepts 4,8");
    test.require(parseBatchMultiplierList(" 4, 8,4 ")
                     .value_or(std::vector<std::size_t>{})
                     == std::vector<std::size_t>({4, 8}),
                 "profile multiplier list trims whitespace and de-duplicates");
    test.require(!parseBatchMultiplierList("4,3"),
                 "profile multiplier list rejects unsupported values");
    test.require(!parseBatchMultiplierList("4,,8"),
                 "profile multiplier list rejects empty entries");
    test.require(std::abs(parsePositiveDouble("8000.5").value_or(0.0) - 8000.5)
                     < 0.0001,
                 "positive double env value is parsed");
    test.require(!parsePositiveDouble("-1"),
                 "negative double env value is rejected");
    test.require(std::abs(parseDepthRatio("0.95").value_or(0.0) - 0.95) < 0.0001,
                 "queue depth ratio env value is parsed");
    test.require(!parseDepthRatio("1.50"),
                 "queue depth ratio above one is rejected");

    pipeline::StageMetricsSnapshot stageMetrics;
    stageMetrics.queueMaxDepth = 95;
    stageMetrics.queueCapacity = 100;
    test.require(std::abs(queueDepthRatio(stageMetrics) - 0.95) < 0.0001,
                 "queue depth ratio is calculated from max depth and capacity");

    AuditResult result;
    result.auditMode = AuditMode::TargetRawShort;
    result.processingMode = pipeline::ProcessingMode::ParallelFanOut;
    result.latencyBudget = LatencyBudget{true, 8.0, 8.0, 0.95};
    result.pipeline.parallelFanOutEndToEndLatency.count = 1;
    result.pipeline.parallelFanOutEndToEndLatency.maxMs = 9.0;
    result.pipeline.waterfallStage.queueWaitLatency.count = 1;
    result.pipeline.waterfallStage.queueWaitLatency.maxMs = 9.0;
    result.pipeline.waterfallStage.queueMaxDepth = 96;
    result.pipeline.waterfallStage.queueCapacity = 100;

    auto evaluation = evaluateLatencyBudget(result);
    test.require(evaluation.fanOutEndToEndStatus == BudgetStatus::Warn,
                 "non-strict fan-out budget violation is a warning");
    test.require(evaluation.stages.front().queueWaitStatus == BudgetStatus::Warn,
                 "non-strict stage queue wait violation is a warning");
    test.require(evaluation.stages.front().queueDepthStatus == BudgetStatus::Warn,
                 "non-strict stage queue depth violation is a warning");

    result.auditMode = AuditMode::TargetRawStrict;
    evaluation = evaluateLatencyBudget(result);
    test.require(evaluation.fanOutEndToEndStatus == BudgetStatus::Fail,
                 "strict fan-out budget violation is a failure");
    test.require(evaluation.stages.front().queueWaitStatus == BudgetStatus::Fail,
                 "strict stage queue wait violation is a failure");
    test.require(evaluation.stages.front().queueDepthStatus == BudgetStatus::Fail,
                 "strict stage queue depth violation is a failure");

    AuditResult noDropResult;
    noDropResult.auditMode = AuditMode::TargetRawShort;
    noDropResult.processingMode = pipeline::ProcessingMode::ParallelFanOut;
    noDropResult.latencyBudget = LatencyBudget{true, 8000.0, 8000.0, 0.95};
    noDropResult.batchSizeMultiplier = 4;
    noDropResult.sourceConfigured = true;
    noDropResult.pipelineStarted = true;
    noDropResult.bridgeStarted = true;
    noDropResult.sourceStarted = true;
    noDropResult.sourceStopped = true;
    noDropResult.bridgeFlushed = true;
    noDropResult.flushed = true;
    noDropResult.source.producedRawBytesPerSecond = 90'000'000.0;
    noDropResult.pipeline.inputSamples = 100;
    noDropResult.pipeline.processedSamples = 100;
    noDropResult.pipeline.parallelFanOutEndToEndLatency.count = 1;
    noDropResult.pipeline.parallelFanOutEndToEndLatency.maxMs = 100.0;

    auto score = makeBatchProfileScore(noDropResult);
    test.require(score.noDropPass,
                 "profile score passes when no drops/rejections/exhaustion are present");
    noDropResult.pipeline.blockPoolExhausted = 1;
    score = makeBatchProfileScore(noDropResult);
    test.require(!score.noDropPass,
                 "profile score fails when block pool is exhausted");

    const auto makeScore = [](std::size_t multiplier,
                              bool noDropPass,
                              bool latencyPass,
                              bool queueDepthPass,
                              double fanOutMaxMs) {
        BatchProfileScore profileScore;
        profileScore.multiplier = multiplier;
        profileScore.noDropPass = noDropPass;
        profileScore.latencyBudgetPass = latencyPass;
        profileScore.queueDepthBudgetPass = queueDepthPass;
        profileScore.fanOutMaxMs = fanOutMaxMs;
        return profileScore;
    };

    auto selection = selectRecommendedBatchProfile({
        makeScore(4, true, true, true, 500.0),
        makeScore(8, true, true, true, 100.0),
    });
    test.require(selection.selected && selection.selected->multiplier == 8,
                 "profile selection chooses lower fan-out max when both pass");

    selection = selectRecommendedBatchProfile({
        makeScore(4, true, true, true, 100.0),
        makeScore(8, true, true, true, 100.0),
    });
    test.require(selection.selected && selection.selected->multiplier == 4,
                 "profile selection tie chooses smaller multiplier");

    selection = selectRecommendedBatchProfile({
        makeScore(4, false, true, true, 100.0),
        makeScore(8, false, true, true, 50.0),
    });
    test.require(!selection.selected,
                 "profile selection returns none when no candidate passes no-drop");

    const auto makeCapacityScore = [](CapacityProfile profile,
                                      bool noDropPass,
                                      bool latencyPass,
                                      bool queueDepthPass,
                                      double maxQueueRatio,
                                      BacklogTrend trend) {
        CapacityProfileScore profileScore;
        profileScore.profile = profile;
        profileScore.profileName = capacityProfileName(profile);
        profileScore.noDropPass = noDropPass;
        profileScore.latencyBudgetPass = latencyPass;
        profileScore.queueDepthBudgetPass = queueDepthPass;
        profileScore.maxQueueDepthRatio = maxQueueRatio;
        profileScore.backlogTrend.overall = trend;
        return profileScore;
    };

    auto capacitySelection = selectRecommendedCapacityProfile({
        makeCapacityScore(CapacityProfile::Current, true, true, true, 0.70, BacklogTrend::Stable),
        makeCapacityScore(CapacityProfile::Balanced1024, true, true, true, 0.70, BacklogTrend::Stable),
    });
    test.require(capacitySelection.selected
                     && capacitySelection.selected->profile == CapacityProfile::Current,
                 "capacity selection chooses smallest qualifying profile");

    capacitySelection = selectRecommendedCapacityProfile({
        makeCapacityScore(CapacityProfile::Current, true, false, true, 0.70, BacklogTrend::Stable),
        makeCapacityScore(CapacityProfile::Balanced1024, true, true, true, 0.70, BacklogTrend::Stable),
    });
    test.require(capacitySelection.selected
                     && capacitySelection.selected->profile == CapacityProfile::Balanced1024,
                 "capacity selection prefers budget-pass profile");

    capacitySelection = selectRecommendedCapacityProfile({
        makeCapacityScore(CapacityProfile::Current, true, true, true, 0.90, BacklogTrend::Stable),
        makeCapacityScore(CapacityProfile::Balanced1024, true, true, true, 0.70, BacklogTrend::Stable),
    });
    test.require(capacitySelection.selected
                     && capacitySelection.selected->profile == CapacityProfile::Balanced1024,
                 "capacity selection prefers max queue ratio below 0.80");

    capacitySelection = selectRecommendedCapacityProfile({
        makeCapacityScore(CapacityProfile::Current, true, true, true, 0.70, BacklogTrend::Growing),
        makeCapacityScore(CapacityProfile::Balanced1024, true, true, true, 0.70, BacklogTrend::Stable),
    });
    test.require(capacitySelection.selected
                     && capacitySelection.selected->profile == CapacityProfile::Balanced1024,
                 "capacity selection prefers stable backlog trend");

    capacitySelection = selectRecommendedCapacityProfile({
        makeCapacityScore(CapacityProfile::Current, false, true, true, 0.70, BacklogTrend::Stable),
        makeCapacityScore(CapacityProfile::Balanced1024, false, true, true, 0.70, BacklogTrend::Stable),
    });
    test.require(!capacitySelection.selected,
                 "capacity selection returns none when all profiles fail no-drop");
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

void baselineRaw60mbpsAccountingSmoke(TestRunner& test)
{
    const auto target = hardware::baselineRawThroughput60MbpsTarget();
    const auto samplesPerBatch = hardware::samplesPerBatchForTarget(target);
    const auto rawBytesPerBatch =
        hardware::rawBytesForSamples(samplesPerBatch, target.packetModel);
    const auto effectiveRawBytesPerSecond = effectiveRawBytesPerSecondFor(target);

    test.require(target.targetBytesPerSecond == kBaselineRawBytesPerSecond,
                 "baseline raw target is 60 MBps");
    test.require(hardware::packetsPerBatchForTarget(target) == 145,
                 "baseline raw packet count is packet-aligned below target");
    test.require(samplesPerBatch == 37'120,
                 "baseline raw samples per batch is packet aligned");
    test.require(rawBytesPerBatch == 598'560,
                 "baseline raw bytes per batch is packet aligned");
    test.require(effectiveRawBytesPerSecond == kExpectedBaselineRawBytesPerSecond,
                 "baseline raw expected throughput is fixed");
}

void baselineRaw60mbpsPipelineSustainAudit(TestRunner& test)
{
    if (!baselineRaw60mbpsPipelineTestEnabled()) {
        return;
    }

    constexpr bool kForceParallelProcessing = true;
    constexpr bool kSignalParameterStageEnabled = false;
    const auto result = runAudit(baselineRaw60mbpsAuditDuration(),
                                 hardware::SimulatorLoadProfile::
                                     BaselineRawThroughput60MBps,
                                 std::chrono::seconds{10},
                                 std::nullopt,
                                 AuditPipelineSizing::Default,
                                 AuditMode::BaselineRaw60,
                                 {},
                                 1,
                                 CapacityProfile::Current,
                                 kForceParallelProcessing,
                                 kSignalParameterStageEnabled);
    printAuditSummary(result);
    printSustainWarnings(result);
    printBaselineRaw60Summary(result);
    const bool passes = baselineRaw60PipelineAcceptancePasses(result);
    assertBaselineRaw60PipelineSustain(test, result);
    if (passes) {
        std::cout << "CONCLUSION: SiriusScope baseline 60 MB/s passes without "
                     "SignalParameter/PRI/PW calculation.\n";
    } else {
        std::cout << "CONCLUSION: SiriusScope baseline 60 MB/s does not pass; "
                     "inspect failed assertions and bottleneck metrics above.\n";
    }
}

void targetRaw90mbpsPipelineSustainAudit(TestRunner& test)
{
    if (!targetRawPipelineTestEnabled() || targetRawBatchSweepEnabled()
        || targetRawProfileSelectionEnabled() || targetRawCapacitySweepEnabled()
        || targetRawSignalParameterAblationEnabled()) {
        return;
    }

    const auto batchSizeMultiplier = envBatchMultiplierOrDefault();
    const auto capacityProfile = envCapacityProfileOrDefault();
    const auto auditMode = strictTargetRawPipelineSustainRequired()
        ? AuditMode::TargetRawStrict
        : AuditMode::TargetRawShort;
    const auto result = runAudit(targetRawAuditDuration(),
                                 hardware::SimulatorLoadProfile::
                                     TargetRawThroughput90MBps,
                                 std::chrono::seconds{10},
                                 std::nullopt,
                                 AuditPipelineSizing::FullTargetRawSustain,
                                 auditMode,
                                 targetRawLatencyBudget(),
                                 batchSizeMultiplier,
                                 capacityProfile);
    printAuditSummary(result);
    printSustainWarnings(result);
    assertTargetRawPipelineSustain(test, result);
}

void targetRaw90mbpsProfileSelectionAudit(TestRunner& test)
{
    if (!targetRawPipelineTestEnabled() || !targetRawProfileSelectionEnabled()
        || targetRawCapacitySweepEnabled()
        || targetRawSignalParameterAblationEnabled()) {
        return;
    }

    if (!parallelProcessingEngineEnabled()) {
        test.require(false,
                     "target raw profile selection requires ParallelFanOut mode");
        return;
    }

    const auto auditMode = targetRawSoakTestEnabled()
        ? AuditMode::TargetRawSoak
        : (strictTargetRawPipelineSustainRequired()
               ? AuditMode::TargetRawStrict
               : AuditMode::TargetRawShort);
    const auto duration = targetRawSoakTestEnabled()
        ? targetRawSoakDuration()
        : targetRawAuditDuration();

    std::vector<AuditResult> results;
    const auto multipliers = profileSelectionMultipliers();
    const auto capacityProfile = envCapacityProfileOrDefault();
    results.reserve(multipliers.size());
    for (const auto multiplier : multipliers) {
        auto result = runAudit(duration,
                               hardware::SimulatorLoadProfile::
                                   TargetRawThroughput90MBps,
                               std::chrono::seconds{10},
                               std::nullopt,
                               AuditPipelineSizing::FullTargetRawSustain,
                               auditMode,
                               targetRawLatencyBudget(),
                               multiplier,
                               capacityProfile);
        printAuditSummary(result);
        printSustainWarnings(result);
        results.push_back(std::move(result));
    }

    const auto scores = makeBatchProfileScores(results);
    const auto selection = selectRecommendedBatchProfile(scores);
    printBatchProfileSelectionSummary(results, scores, selection);

    test.require(!results.empty(),
                 "profile selection has at least one candidate multiplier");
    if (strictTargetRawPipelineSustainRequired()) {
        test.require(selection.selected.has_value(),
                     "strict profile selection finds a no-drop candidate");
    }
    if (selection.selected && latencyBudgetIsHard(results.front())) {
        test.require(batchProfileBudgetPasses(*selection.selected),
                     "strict/soak profile selection selected candidate satisfies latency/backlog budget");
    }
}

void targetRaw90mbpsCapacitySweepAudit(TestRunner& test)
{
    if (!targetRawPipelineTestEnabled() || !targetRawCapacitySweepEnabled()
        || targetRawSignalParameterAblationEnabled()) {
        return;
    }

    if (!parallelProcessingEngineEnabled()) {
        test.require(false,
                     "target raw capacity sweep requires ParallelFanOut mode");
        return;
    }

    constexpr std::size_t kCapacitySweepBatchMultiplier = 8;
    const auto auditMode = targetRawSoakTestEnabled()
        ? AuditMode::TargetRawSoak
        : (strictTargetRawPipelineSustainRequired()
               ? AuditMode::TargetRawStrict
               : AuditMode::TargetRawShort);
    const auto duration = targetRawSoakTestEnabled()
        ? targetRawSoakDuration()
        : targetRawAuditDuration();

    std::vector<AuditResult> results;
    const auto profiles = capacitySweepProfiles();
    results.reserve(profiles.size());
    for (const auto profile : profiles) {
        auto result = runAudit(duration,
                               hardware::SimulatorLoadProfile::
                                   TargetRawThroughput90MBps,
                               std::chrono::seconds{10},
                               std::nullopt,
                               AuditPipelineSizing::FullTargetRawSustain,
                               auditMode,
                               targetRawLatencyBudget(),
                               kCapacitySweepBatchMultiplier,
                               profile);
        printAuditSummary(result);
        printSustainWarnings(result);
        results.push_back(std::move(result));
    }

    const auto scores = makeCapacityProfileScores(results);
    const auto selection = selectRecommendedCapacityProfile(scores);
    printCapacityProfileSweepSummary(results, scores, selection);

    test.require(!results.empty(),
                 "capacity sweep has at least one candidate profile");
    if (strictTargetRawPipelineSustainRequired()) {
        test.require(selection.selected.has_value(),
                     "strict capacity sweep finds a no-drop profile");
    }
    if (selection.selected && latencyBudgetIsHard(results.front())) {
        test.require(capacityProfileBudgetPasses(*selection.selected),
                     "strict/soak capacity sweep selected profile satisfies latency/backlog budget");
    }
}

void targetRaw90mbpsSignalParameterAblationAudit(TestRunner& test)
{
    if (!targetRawSignalParameterAblationEnabled()) {
        return;
    }

    constexpr std::size_t kAblationBatchMultiplier = 8;
    const auto duration = targetRawSoakDuration();
    const auto capacityProfile = envCapacityProfileOrDefault();
    const auto auditMode = AuditMode::TargetRawSoak;
    constexpr bool kForceParallelProcessing = true;

    auto baseline = runAudit(duration,
                             hardware::SimulatorLoadProfile::TargetRawThroughput90MBps,
                             std::chrono::seconds{10},
                             std::nullopt,
                             AuditPipelineSizing::FullTargetRawSustain,
                             auditMode,
                             targetRawLatencyBudget(),
                             kAblationBatchMultiplier,
                             capacityProfile,
                             kForceParallelProcessing,
                             true);
    printAuditSummary(baseline);
    printSustainWarnings(baseline);

    auto ablation = runAudit(duration,
                             hardware::SimulatorLoadProfile::TargetRawThroughput90MBps,
                             std::chrono::seconds{10},
                             std::nullopt,
                             AuditPipelineSizing::FullTargetRawSustain,
                             auditMode,
                             targetRawLatencyBudget(),
                             kAblationBatchMultiplier,
                             capacityProfile,
                             kForceParallelProcessing,
                             false);
    printAuditSummary(ablation);
    printSustainWarnings(ablation);

    printSignalParameterAblationSummary(baseline, ablation);

    test.require(auditInfrastructureSucceeded(baseline),
                 "signal parameter ablation baseline infrastructure succeeds");
    test.require(auditInfrastructureSucceeded(ablation),
                 "signal parameter ablation run infrastructure succeeds");
    test.require(rawThroughputValid(baseline),
                 "signal parameter ablation baseline raw throughput is usable");
    test.require(rawThroughputValid(ablation),
                 "signal parameter ablation run raw throughput is usable");
    test.require(baseline.signalParameterStageEnabled,
                 "signal parameter ablation baseline enables signal parameter stage");
    test.require(!ablation.signalParameterStageEnabled,
                 "signal parameter ablation run disables signal parameter stage");
    test.require(ablation.hasSpectrumSnapshot,
                 "signal parameter ablation run keeps spectrum stage active");
    test.require(ablation.hasBearingSnapshot || ablation.pipeline.producedBearingSnapshots == 0,
                 "signal parameter ablation run keeps bearing stage active when snapshots are produced");
    test.require(!ablation.hasSignalParameterSnapshot,
                 "signal parameter ablation run publishes no signal parameter snapshot");
    test.require(ablation.pipeline.signalParameterStageProcessedBlocks == 0,
                 "signal parameter ablation run sends no blocks to signal parameter stage");
}

void targetRaw90mbpsBatchSweepAudit(TestRunner& test)
{
    if (!targetRawBatchSweepEnabled() || targetRawProfileSelectionEnabled()
        || targetRawCapacitySweepEnabled()
        || targetRawSignalParameterAblationEnabled()) {
        return;
    }

    std::vector<AuditResult> results;
    for (const auto multiplier : batchSweepMultipliers()) {
        auto result = runAudit(targetRawAuditDuration(),
                               hardware::SimulatorLoadProfile::
                                   TargetRawThroughput90MBps,
                               std::chrono::seconds{10},
                               std::nullopt,
                               AuditPipelineSizing::FullTargetRawSustain,
                               AuditMode::TargetRawShort,
                               targetRawLatencyBudget(),
                               multiplier,
                               envCapacityProfileOrDefault());
        printAuditSummary(result);
        printSustainWarnings(result);
        assertBatchSweepRunUsable(test, result);
        results.push_back(std::move(result));
    }

    printBatchSweepSummary(results);
}

void targetRaw90mbpsParallelSoakAudit(TestRunner& test)
{
    if (!targetRawPipelineTestEnabled() || !parallelProcessingEngineEnabled()
        || !targetRawSoakTestEnabled() || targetRawBatchSweepEnabled()
        || targetRawProfileSelectionEnabled() || targetRawCapacitySweepEnabled()
        || targetRawSignalParameterAblationEnabled()) {
        return;
    }

    const auto batchSizeMultiplier = envBatchMultiplierOrDefault();
    const auto capacityProfile = envCapacityProfileOrDefault();
    const auto result = runAudit(targetRawSoakDuration(),
                                 hardware::SimulatorLoadProfile::
                                     TargetRawThroughput90MBps,
                                 std::chrono::seconds{10},
                                 std::nullopt,
                                 AuditPipelineSizing::FullTargetRawSustain,
                                 AuditMode::TargetRawSoak,
                                 targetRawLatencyBudget(),
                                 batchSizeMultiplier,
                                 capacityProfile);
    printAuditSummary(result);
    printSustainWarnings(result);
    assertTargetRawPipelineSustain(test, result);
}

} // namespace

int main()
{
    TestRunner test;

    testAuditHelperParsingAndBudget(test);
    highLoadDataPlaneSmoke(test);
    baselineRaw60mbpsAccountingSmoke(test);
    baselineRaw60mbpsPipelineSustainAudit(test);
    targetRaw90mbpsAccountingSmoke(test);
    targetRaw90mbpsSignalParameterAblationAudit(test);
    targetRaw90mbpsPipelineSustainAudit(test);
    targetRaw90mbpsProfileSelectionAudit(test);
    targetRaw90mbpsCapacitySweepAudit(test);
    targetRaw90mbpsBatchSweepAudit(test);
    targetRaw90mbpsParallelSoakAudit(test);
    highLoadDataPlaneStress(test);

    return test.result();
}
