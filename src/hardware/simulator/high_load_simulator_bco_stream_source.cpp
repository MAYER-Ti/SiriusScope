#include "hardware/simulator/high_load_simulator_bco_stream_source.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace siriusscope::hardware {

namespace {

constexpr std::chrono::milliseconds kMinBatchPeriod{1};
constexpr std::size_t kFrequencyBinCount = 128;
constexpr std::uint64_t kSamplesPerPacket = 256;

SimulatorBcoLoadConfig makeSimulatorBcoLoadConfig(SimulatorLoadProfile profile)
{
    SimulatorBcoLoadConfig config;
    config.profile = profile;

    switch (profile) {
    case SimulatorLoadProfile::UiDemo:
        config.samplesPerSecond = 1'280;
        config.batchPeriod = std::chrono::milliseconds{100};
        config.burstModeEnabled = false;
        break;
    case SimulatorLoadProfile::MediumLoad:
        config.samplesPerSecond = 250'000;
        config.batchPeriod = std::chrono::milliseconds{10};
        config.burstModeEnabled = false;
        break;
    case SimulatorLoadProfile::RealBcoEquivalent:
        config.samplesPerSecond = 1'000'000;
        config.batchPeriod = std::chrono::milliseconds{10};
        config.burstModeEnabled = false;
        break;
    case SimulatorLoadProfile::Stress150Percent:
        config.samplesPerSecond = 1'500'000;
        config.batchPeriod = std::chrono::milliseconds{10};
        config.burstModeEnabled = true;
        config.burstMultiplier = 2.0;
        config.burstDuration = std::chrono::milliseconds{20};
        config.calmDuration = std::chrono::milliseconds{80};
        break;
    }

    return config;
}

SimulatorBcoLoadConfig normalizeLoadConfig(SimulatorBcoLoadConfig config)
{
    if (config.profile == SimulatorLoadProfile::UiDemo) {
        const auto requestedSamplesPerSecond = config.samplesPerSecond;
        const auto requestedBatchPeriod = config.batchPeriod;
        const auto requestedDeterministic = config.deterministic;

        config = makeSimulatorBcoLoadConfig(SimulatorLoadProfile::UiDemo);
        config.samplesPerSecond = requestedSamplesPerSecond;
        config.batchPeriod = requestedBatchPeriod;
        config.deterministic = requestedDeterministic;
    } else {
        config = makeSimulatorBcoLoadConfig(config.profile);
    }

    config.samplesPerSecond = std::max<std::size_t>(1, config.samplesPerSecond);
    if (config.batchPeriod < kMinBatchPeriod) {
        config.batchPeriod = kMinBatchPeriod;
    }
    if (!std::isfinite(config.burstMultiplier) || config.burstMultiplier < 1.0) {
        config.burstMultiplier = 1.0;
    }

    return config;
}

std::vector<core::BandConfig> enabledBandsFrom(const BcoStreamConfig& config)
{
    std::vector<core::BandConfig> enabledBands;
    enabledBands.reserve(config.bandConfigs.size());

    for (const auto& bandConfig : config.bandConfigs) {
        if (bandConfig.enabled) {
            enabledBands.push_back(bandConfig);
        }
    }

    return enabledBands;
}

std::size_t boundedSizeFrom(long double value)
{
    if (value <= 1.0L) {
        return 1;
    }

    const auto maxSize = static_cast<long double>(std::numeric_limits<std::size_t>::max());
    if (value >= maxSize) {
        return std::numeric_limits<std::size_t>::max();
    }

    return static_cast<std::size_t>(std::ceil(value));
}

std::uint64_t packetCountFor(std::size_t sampleCount)
{
    if (sampleCount == 0) {
        return 0;
    }

    return std::max<std::uint64_t>(
        1,
        static_cast<std::uint64_t>(sampleCount) / kSamplesPerPacket);
}

} // namespace

HighLoadSimulatorBcoStreamSource::HighLoadSimulatorBcoStreamSource(
    SimulatorBcoLoadConfig loadConfig,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_loadConfig(normalizeLoadConfig(loadConfig))
    , m_diagnosticsSink(diagnosticsSink)
{
}

HighLoadSimulatorBcoStreamSource::~HighLoadSimulatorBcoStreamSource()
{
    stop();
}

core::OperationResult HighLoadSimulatorBcoStreamSource::configure(
    const BcoStreamConfig& config)
{
    if (config.bandConfigs.empty()) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "high-load BCO simulator configuration rejected: no band configs");
        return core::OperationResult::failure("BCO stream config must contain band configs");
    }

    const auto enabledBands = enabledBandsFrom(config);
    if (enabledBands.empty()) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "high-load BCO simulator configuration rejected: no enabled bands");
        return core::OperationResult::failure("BCO stream config must contain enabled bands");
    }

    std::lock_guard lock(m_mutex);
    if (m_running) {
        return core::OperationResult::failure(
            "high-load BCO simulator cannot be configured while running");
    }

    m_streamConfig = config;
    m_metrics = {};
    m_nextSampleIndex = config.timeBase.firstSampleIndex;
    m_generatedBatchIndex = 0;
    m_startedAt = {};
    m_configured = true;
    m_stopRequested = false;

    return core::OperationResult::ok();
}

core::OperationResult HighLoadSimulatorBcoStreamSource::start(
    SampleBlockCallback callback)
{
    if (!callback) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "high-load BCO simulator start rejected: callback is empty");
        return core::OperationResult::failure("BCO stream callback must not be empty");
    }

    std::lock_guard lock(m_mutex);
    if (!m_configured) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "high-load BCO simulator start rejected: source is not configured");
        return core::OperationResult::failure("BCO stream source is not configured");
    }

    if (m_running) {
        return core::OperationResult::ok();
    }

    m_stopRequested = false;
    m_running = true;
    m_startedAt = std::chrono::steady_clock::now();
    m_worker = std::thread(&HighLoadSimulatorBcoStreamSource::generationLoop,
                           this,
                           std::move(callback));

    return core::OperationResult::ok();
}

core::OperationResult HighLoadSimulatorBcoStreamSource::stop()
{
    std::thread worker;

    {
        std::lock_guard lock(m_mutex);
        if (!m_running && !m_worker.joinable()) {
            return core::OperationResult::ok();
        }

        m_stopRequested = true;
        if (m_worker.joinable()) {
            if (m_worker.get_id() == std::this_thread::get_id()) {
                m_worker.detach();
            } else {
                worker = std::move(m_worker);
            }
        }
    }

    m_condition.notify_all();

    if (worker.joinable()) {
        worker.join();
    }

    {
        std::lock_guard lock(m_mutex);
        m_running = false;
        m_stopRequested = false;
    }

    return core::OperationResult::ok();
}

BcoSourceMetrics HighLoadSimulatorBcoStreamSource::metrics() const
{
    std::lock_guard lock(m_mutex);
    return m_metrics;
}

void HighLoadSimulatorBcoStreamSource::generationLoop(SampleBlockCallback callback)
{
    for (;;) {
        std::uint64_t batchIndex = 0;
        {
            std::lock_guard lock(m_mutex);
            if (m_stopRequested) {
                break;
            }
            batchIndex = m_generatedBatchIndex++;
        }

        auto block = generateBlock(samplesPerBatchForCurrentPeriod(batchIndex));

        const auto callbackStartedAt = std::chrono::steady_clock::now();
        callback(block);
        const auto callbackDuration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - callbackStartedAt);

        updateMetricsAfterBlock(*block, callbackDuration);

        std::unique_lock lock(m_mutex);
        if (m_condition.wait_for(lock, m_loadConfig.batchPeriod, [this] {
                return m_stopRequested;
            })) {
            break;
        }
    }

    std::lock_guard lock(m_mutex);
    m_running = false;
}

std::shared_ptr<const BcoSampleBlock> HighLoadSimulatorBcoStreamSource::generateBlock(
    std::size_t sampleCount)
{
    std::vector<core::BandConfig> enabledBands;
    std::uint64_t firstSampleIndex = 0;

    {
        std::lock_guard lock(m_mutex);
        enabledBands = enabledBandsFrom(m_streamConfig);
        firstSampleIndex = m_nextSampleIndex;
    }

    auto block = std::make_shared<BcoSampleBlock>();
    block->samples.reserve(sampleCount);

    // TODO: Add per-band PRI/PW pulse model in the next high-load simulator substage.
    for (std::size_t i = 0; i < sampleCount && !enabledBands.empty(); ++i) {
        const auto& band = enabledBands[i % enabledBands.size()];
        const auto maxOffsetHz = std::max<std::int64_t>(0, band.widthHz / 2);
        const auto binOffset = static_cast<std::int64_t>(i % kFrequencyBinCount);
        const auto offsetStepHz =
            maxOffsetHz == 0
                ? 0
                : std::max<std::int64_t>(
                    1,
                    (maxOffsetHz * 2) / static_cast<std::int64_t>(kFrequencyBinCount));
        auto frequencyOffsetHz = -maxOffsetHz + binOffset * offsetStepHz;
        frequencyOffsetHz = std::clamp(frequencyOffsetHz, -maxOffsetHz, maxOffsetHz);

        const core::BeamSample beamSample{
            firstSampleIndex + static_cast<std::uint64_t>(i),
            frequencyOffsetHz,
            20 + static_cast<int>(i % 100),
            static_cast<int>(i % core::DomainConstraints::currentBeamCount),
        };

        const auto sample = core::SignalSample::create(beamSample, band);
        if (sample) {
            block->samples.push_back(*sample.value());
        }
    }

    {
        std::lock_guard lock(m_mutex);
        m_nextSampleIndex += static_cast<std::uint64_t>(block->samples.size());
    }

    const auto actualSampleCount = block->samples.size();
    block->stats.sampleCount = static_cast<std::uint64_t>(actualSampleCount);
    block->stats.packetCount = packetCountFor(actualSampleCount);
    block->stats.lostPacketCount = 0;
    block->stats.malformedPacketCount = 0;
    block->stats.producedAt = std::chrono::steady_clock::now();

    if (!block->samples.empty()) {
        block->stats.firstSampleIndex = block->samples.front().sampleIndex;
        block->stats.lastSampleIndex = block->samples.back().sampleIndex;
    }

    return block;
}

std::size_t HighLoadSimulatorBcoStreamSource::samplesPerBatchForCurrentPeriod(
    std::uint64_t batchIndex) const
{
    const auto batchPeriodMs = std::max<std::int64_t>(1, m_loadConfig.batchPeriod.count());
    const long double baseSamples =
        static_cast<long double>(m_loadConfig.samplesPerSecond)
        * static_cast<long double>(batchPeriodMs) / 1000.0L;
    const auto baseSampleCount = boundedSizeFrom(baseSamples);

    if (!m_loadConfig.burstModeEnabled || m_loadConfig.burstDuration.count() <= 0
        || m_loadConfig.calmDuration.count() <= 0) {
        return baseSampleCount;
    }

    const auto cycleDuration = m_loadConfig.calmDuration + m_loadConfig.burstDuration;
    if (cycleDuration.count() <= 0) {
        return baseSampleCount;
    }

    const auto cycleMs = static_cast<std::uint64_t>(cycleDuration.count());
    const auto periodMs = static_cast<std::uint64_t>(batchPeriodMs);
    const auto positionInCycleMs = (batchIndex * periodMs) % cycleMs;
    const auto calmDurationMs = static_cast<std::uint64_t>(m_loadConfig.calmDuration.count());

    if (positionInCycleMs < calmDurationMs) {
        return baseSampleCount;
    }

    return boundedSizeFrom(static_cast<long double>(baseSampleCount)
                           * static_cast<long double>(m_loadConfig.burstMultiplier));
}

void HighLoadSimulatorBcoStreamSource::updateMetricsAfterBlock(
    const BcoSampleBlock& block,
    std::chrono::milliseconds callbackDuration)
{
    std::lock_guard lock(m_mutex);

    m_metrics.producedSamples += block.stats.sampleCount;
    ++m_metrics.producedBatches;
    m_metrics.lostPackets += block.stats.lostPacketCount;
    m_metrics.malformedPackets += block.stats.malformedPacketCount;

    const auto elapsedSeconds =
        std::chrono::duration<double>(block.stats.producedAt - m_startedAt).count();
    if (elapsedSeconds > 0.0) {
        m_metrics.producedSamplesPerSecond =
            static_cast<double>(m_metrics.producedSamples) / elapsedSeconds;
        m_metrics.equivalentMegabytesPerSecond =
            m_metrics.producedSamplesPerSecond
            * static_cast<double>(sizeof(core::SignalSample)) / (1024.0 * 1024.0);
    }

    if (callbackDuration > m_metrics.maxCallbackDuration) {
        m_metrics.maxCallbackDuration = callbackDuration;
    }
}

void HighLoadSimulatorBcoStreamSource::publish(
    infrastructure::DiagnosticSeverity severity,
    const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "HighLoadSimulatorBcoStreamSource",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::hardware
