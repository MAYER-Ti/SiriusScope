#include "hardware/simulator/high_load_simulator_bco_stream_source.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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
    auto requestedPulseConfigs = std::move(config.pulseBandConfigs);

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
    config.pulseBandConfigs = std::move(requestedPulseConfigs);

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

std::uint64_t saturatedAdd(std::uint64_t value, std::uint64_t increment)
{
    if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
        return std::numeric_limits<std::uint64_t>::max();
    }

    return value + increment;
}

const SimulatorPulseBandConfig* pulseConfigForBand(
    const std::vector<SimulatorPulseBandConfig>& configs,
    int bandIndex)
{
    const auto found = std::find_if(configs.begin(), configs.end(), [bandIndex](const auto& item) {
        return item.bandIndex == bandIndex;
    });
    return found == configs.end() ? nullptr : &(*found);
}

bool hasValidPulseTiming(const SimulatorPulseBandConfig& config)
{
    if (!config.enabled || !std::isfinite(config.pulsePeriodUs)
        || !std::isfinite(config.pulseWidthUs) || config.pulsePeriodUs <= 0.0
        || config.pulseWidthUs <= 0.0 || config.pulseWidthUs >= config.pulsePeriodUs) {
        return false;
    }

    const long double periodNs = static_cast<long double>(config.pulsePeriodUs) * 1000.0L;
    const long double widthNs = static_cast<long double>(config.pulseWidthUs) * 1000.0L;
    return periodNs > 0.0L && widthNs > 0.0L && widthNs < periodNs;
}

bool isInsidePulse(std::uint64_t sampleIndex,
                   std::uint64_t firstSampleIndex,
                   std::uint64_t samplePeriodNs,
                   const SimulatorPulseBandConfig& config)
{
    if (!hasValidPulseTiming(config)) {
        return false;
    }

    const auto safeSamplePeriodNs = std::max<std::uint64_t>(1, samplePeriodNs);
    const auto relativeSampleIndex =
        sampleIndex >= firstSampleIndex ? sampleIndex - firstSampleIndex : 0;
    const long double relativeNs =
        static_cast<long double>(relativeSampleIndex)
        * static_cast<long double>(safeSamplePeriodNs);
    const long double periodNs = static_cast<long double>(config.pulsePeriodUs) * 1000.0L;
    const long double widthNs = static_cast<long double>(config.pulseWidthUs) * 1000.0L;

    auto phaseNs = std::fmod(relativeNs, periodNs);
    if (phaseNs < 0.0L) {
        phaseNs += periodNs;
    }

    return phaseNs >= 0.0L && phaseNs < widthNs;
}

std::uint64_t nextPulseStartSampleIndex(std::uint64_t sampleIndex,
                                        std::uint64_t firstSampleIndex,
                                        std::uint64_t samplePeriodNs,
                                        const SimulatorPulseBandConfig& config)
{
    if (!hasValidPulseTiming(config)) {
        return saturatedAdd(sampleIndex, 1);
    }
    if (isInsidePulse(sampleIndex, firstSampleIndex, samplePeriodNs, config)) {
        return sampleIndex;
    }

    const auto safeSamplePeriodNs = std::max<std::uint64_t>(1, samplePeriodNs);
    const auto relativeSampleIndex =
        sampleIndex >= firstSampleIndex ? sampleIndex - firstSampleIndex : 0;
    const long double relativeNs =
        static_cast<long double>(relativeSampleIndex)
        * static_cast<long double>(safeSamplePeriodNs);
    const long double periodNs = static_cast<long double>(config.pulsePeriodUs) * 1000.0L;

    auto phaseNs = std::fmod(relativeNs, periodNs);
    if (phaseNs < 0.0L) {
        phaseNs += periodNs;
    }

    const long double nextRelativeNs = relativeNs + periodNs - phaseNs;
    const long double candidateValue =
        std::ceil(nextRelativeNs / static_cast<long double>(safeSamplePeriodNs));
    const auto maxRelativeSampleIndex =
        std::numeric_limits<std::uint64_t>::max() - firstSampleIndex;
    if (candidateValue > static_cast<long double>(maxRelativeSampleIndex)) {
        return std::numeric_limits<std::uint64_t>::max();
    }

    auto candidate =
        saturatedAdd(firstSampleIndex, static_cast<std::uint64_t>(candidateValue));
    if (candidate <= sampleIndex) {
        candidate = saturatedAdd(sampleIndex, 1);
    }

    return candidate;
}

bool enabledBandsContain(const std::vector<core::BandConfig>& bands, int bandIndex)
{
    return std::any_of(bands.begin(), bands.end(), [bandIndex](const auto& band) {
        return band.bandIndex == bandIndex;
    });
}

std::uint64_t alignToBandSlot(std::uint64_t sampleIndex,
                              std::uint64_t batchStartSampleIndex,
                              std::size_t bandSlot,
                              std::size_t bandCount)
{
    if (bandCount == 0 || sampleIndex < batchStartSampleIndex) {
        return sampleIndex;
    }

    const auto relativeSampleIndex = sampleIndex - batchStartSampleIndex;
    const auto remainder =
        relativeSampleIndex % static_cast<std::uint64_t>(bandCount);
    const auto target = static_cast<std::uint64_t>(bandSlot);
    const auto delta =
        target >= remainder
            ? target - remainder
            : static_cast<std::uint64_t>(bandCount) - remainder + target;

    return saturatedAdd(sampleIndex, delta);
}

void keepNearest(std::optional<std::uint64_t>& nearest, std::uint64_t candidate)
{
    if (!nearest || candidate < *nearest) {
        nearest = candidate;
    }
}

std::optional<std::uint64_t> nextGeneratablePulseSampleIndex(
    std::uint64_t sampleIndex,
    std::uint64_t batchStartSampleIndex,
    std::uint64_t batchEndSampleIndex,
    std::uint64_t timeBaseFirstSampleIndex,
    std::uint64_t samplePeriodNs,
    const std::vector<core::BandConfig>& enabledBands,
    const std::vector<SimulatorPulseBandConfig>& pulseConfigs)
{
    std::optional<std::uint64_t> nearest;
    const auto bandCount = enabledBands.size();

    for (std::size_t bandSlot = 0; bandSlot < bandCount; ++bandSlot) {
        const auto& band = enabledBands[bandSlot];
        auto candidate = alignToBandSlot(sampleIndex,
                                         batchStartSampleIndex,
                                         bandSlot,
                                         bandCount);
        if (candidate >= batchEndSampleIndex) {
            continue;
        }

        const auto* pulseConfig = pulseConfigForBand(pulseConfigs, band.bandIndex);
        if (!pulseConfig) {
            keepNearest(nearest, candidate);
            continue;
        }
        if (!pulseConfig->enabled) {
            continue;
        }
        if (!hasValidPulseTiming(*pulseConfig)) {
            keepNearest(nearest, candidate);
            continue;
        }

        while (candidate < batchEndSampleIndex) {
            if (isInsidePulse(candidate,
                              timeBaseFirstSampleIndex,
                              samplePeriodNs,
                              *pulseConfig)) {
                keepNearest(nearest, candidate);
                break;
            }

            const auto nextPulseStart = nextPulseStartSampleIndex(candidate,
                                                                  timeBaseFirstSampleIndex,
                                                                  samplePeriodNs,
                                                                  *pulseConfig);
            if (nextPulseStart <= candidate) {
                candidate = saturatedAdd(candidate, static_cast<std::uint64_t>(bandCount));
            } else {
                candidate = alignToBandSlot(nextPulseStart,
                                            batchStartSampleIndex,
                                            bandSlot,
                                            bandCount);
            }
        }
    }

    return nearest;
}

void appendGeneratedSample(BcoSampleBlock& block,
                           const core::BandConfig& band,
                           std::uint64_t sampleIndex,
                           std::uint64_t sequenceIndex)
{
    const auto maxOffsetHz = std::max<std::int64_t>(0, band.widthHz / 2);
    const auto binOffset = static_cast<std::int64_t>(sequenceIndex % kFrequencyBinCount);
    const auto offsetStepHz =
        maxOffsetHz == 0
            ? 0
            : std::max<std::int64_t>(
                1,
                (maxOffsetHz * 2) / static_cast<std::int64_t>(kFrequencyBinCount));
    auto frequencyOffsetHz = -maxOffsetHz + binOffset * offsetStepHz;
    frequencyOffsetHz = std::clamp(frequencyOffsetHz, -maxOffsetHz, maxOffsetHz);

    const core::BeamSample beamSample{
        sampleIndex,
        frequencyOffsetHz,
        20 + static_cast<int>(sequenceIndex % 100),
        static_cast<int>(sequenceIndex % core::DomainConstraints::currentBeamCount),
    };

    const auto sample = core::SignalSample::create(beamSample, band);
    if (sample) {
        block.samples.push_back(*sample.value());
    }
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
    std::vector<SimulatorPulseBandConfig> pulseConfigs;
    {
        std::lock_guard lock(m_mutex);
        pulseConfigs = m_loadConfig.pulseBandConfigs;
    }

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

    if (!pulseConfigs.empty()) {
        for (const auto& pulseConfig : pulseConfigs) {
            if (!enabledBandsContain(enabledBands, pulseConfig.bandIndex)) {
                publish(infrastructure::DiagnosticSeverity::Warning,
                        "high-load BCO simulator pulse config references unknown or disabled bandIndex "
                            + std::to_string(pulseConfig.bandIndex));
            }
            if (pulseConfig.enabled && !hasValidPulseTiming(pulseConfig)) {
                publish(infrastructure::DiagnosticSeverity::Warning,
                        "high-load BCO simulator pulse config has invalid PRI/PW for bandIndex "
                            + std::to_string(pulseConfig.bandIndex));
            }
        }

        const bool allEnabledBandsDisabledByPulseConfig =
            std::all_of(enabledBands.begin(), enabledBands.end(), [&pulseConfigs](const auto& band) {
                const auto* pulseConfig =
                    pulseConfigForBand(pulseConfigs, band.bandIndex);
                return pulseConfig && !pulseConfig->enabled;
            });
        if (allEnabledBandsDisabledByPulseConfig) {
            publish(infrastructure::DiagnosticSeverity::Warning,
                    "high-load BCO simulator pulse configs disable all enabled bands");
        }
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

void HighLoadSimulatorBcoStreamSource::setPulseBandConfigs(
    std::vector<SimulatorPulseBandConfig> configs)
{
    std::lock_guard lock(m_mutex);
    m_loadConfig.pulseBandConfigs = std::move(configs);
}

std::vector<SimulatorPulseBandConfig> HighLoadSimulatorBcoStreamSource::pulseBandConfigs() const
{
    std::lock_guard lock(m_mutex);
    return m_loadConfig.pulseBandConfigs;
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
    std::uint64_t batchStartSampleIndex = 0;
    std::uint64_t timeBaseFirstSampleIndex = 0;
    std::uint64_t samplePeriodNs = 1;
    std::vector<SimulatorPulseBandConfig> pulseConfigs;

    {
        std::lock_guard lock(m_mutex);
        enabledBands = enabledBandsFrom(m_streamConfig);
        batchStartSampleIndex = m_nextSampleIndex;
        timeBaseFirstSampleIndex = m_streamConfig.timeBase.firstSampleIndex;
        samplePeriodNs = std::max<std::uint64_t>(1, m_streamConfig.timeBase.samplePeriodNs);
        pulseConfigs = m_loadConfig.pulseBandConfigs;
    }

    const auto sampleSlotsInBatch =
        sampleCount > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())
            ? std::numeric_limits<std::uint64_t>::max()
            : static_cast<std::uint64_t>(sampleCount);
    const auto batchEndSampleIndex =
        saturatedAdd(batchStartSampleIndex, sampleSlotsInBatch);

    auto block = std::make_shared<BcoSampleBlock>();
    block->samples.reserve(sampleCount);

    if (!enabledBands.empty() && sampleSlotsInBatch > 0) {
        if (pulseConfigs.empty()) {
            for (std::uint64_t i = 0; i < sampleSlotsInBatch; ++i) {
                const auto& band =
                    enabledBands[static_cast<std::size_t>(i % enabledBands.size())];
                appendGeneratedSample(*block,
                                      band,
                                      saturatedAdd(batchStartSampleIndex, i),
                                      i);
            }
        } else {
            auto sampleIndex = batchStartSampleIndex;
            while (sampleIndex < batchEndSampleIndex) {
                const auto nextSampleIndex = nextGeneratablePulseSampleIndex(
                    sampleIndex,
                    batchStartSampleIndex,
                    batchEndSampleIndex,
                    timeBaseFirstSampleIndex,
                    samplePeriodNs,
                    enabledBands,
                    pulseConfigs);
                if (!nextSampleIndex) {
                    break;
                }

                sampleIndex = *nextSampleIndex;
                const auto sequenceIndex = sampleIndex - batchStartSampleIndex;
                const auto& band =
                    enabledBands[static_cast<std::size_t>(sequenceIndex
                                                          % enabledBands.size())];
                appendGeneratedSample(*block, band, sampleIndex, sequenceIndex);
                sampleIndex = saturatedAdd(sampleIndex, 1);
            }
        }
    }

    {
        std::lock_guard lock(m_mutex);
        m_nextSampleIndex = batchEndSampleIndex;
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
    } else {
        block->stats.firstSampleIndex = batchStartSampleIndex;
        block->stats.lastSampleIndex = batchStartSampleIndex;
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
