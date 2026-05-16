#include "hardware/simulator/simulator_bco_sample_source.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

namespace siriusscope::hardware {

namespace {

constexpr std::array<std::int64_t, 5> kDefaultCentersHz = {
    3'000'000'000LL,
    5'795'000'000LL,
    8'250'000'000LL,
    9'550'000'000LL,
    14'250'000'000LL,
};

constexpr std::array<std::int64_t, 5> kDefaultWidthsHz = {
    500'000'000LL,
    410'000'000LL,
    500'000'000LL,
    500'000'000LL,
    500'000'000LL,
};

std::vector<core::BandConfig> makeDefaultBandConfigs()
{
    std::vector<core::BandConfig> configs;
    configs.reserve(core::DomainConstraints::currentBandCount);

    for (int bandIndex = 0; bandIndex < core::DomainConstraints::currentBandCount; ++bandIndex) {
        const auto created = core::BandConfig::create(bandIndex,
                                                      kDefaultCentersHz[static_cast<std::size_t>(
                                                          bandIndex)],
                                                      kDefaultWidthsHz[static_cast<std::size_t>(
                                                          bandIndex)]);
        if (created) {
            configs.push_back(*created.value());
        }
    }

    return configs;
}

std::int64_t clampOffset(const core::BandConfig& config, std::int64_t offsetHz)
{
    const auto maxOffsetHz = config.widthHz / 2;
    return std::clamp(offsetHz, -maxOffsetHz, maxOffsetHz);
}

std::int64_t deterministicOffset(const core::BandConfig& config, std::uint64_t signalStep)
{
    switch (config.bandIndex) {
    case 0:
        return clampOffset(config, -80'000'000LL);
    case 1:
        return clampOffset(config, 30'000'000LL);
    case 2:
        return 0;
    case 3: {
        const bool highSide = ((signalStep / 8U) % 2U) == 0U;
        return clampOffset(config, highSide ? 120'000'000LL : -120'000'000LL);
    }
    case 4: {
        const auto maxOffsetHz = config.widthHz / 2;
        if (maxOffsetHz <= 0) {
            return 0;
        }

        const auto sweepStep = static_cast<std::int64_t>(signalStep % 11U) - 5;
        const auto sweep = sweepStep * (maxOffsetHz / 6);
        return clampOffset(config, sweep);
    }
    default:
        return 0;
    }
}

int deterministicAmplitude(int bandIndex, int beamIndex, std::uint64_t signalStep)
{
    int baseAmplitude = 32;
    switch (bandIndex) {
    case 0:
        baseAmplitude = 96 + static_cast<int>(signalStep % 4U);
        break;
    case 1:
        baseAmplitude = 88 + static_cast<int>((signalStep / 2U) % 5U);
        break;
    case 2:
        baseAmplitude = 102;
        break;
    case 3:
        baseAmplitude = ((signalStep / 8U) % 2U) == 0U ? 76 : 24;
        break;
    case 4:
        baseAmplitude = 18 + static_cast<int>(signalStep % 3U);
        break;
    default:
        baseAmplitude = 40;
        break;
    }

    const int beamDelta = beamIndex == 0 ? 0 : 7;
    return std::clamp(baseAmplitude - beamDelta,
                      core::DomainConstraints::minAmplitude,
                      core::DomainConstraints::maxAmplitude);
}

} // namespace

SimulatorBcoSampleSource::SimulatorBcoSampleSource(
    SimulatorBcoSampleSourceConfig config,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_config(std::move(config))
    , m_diagnosticsSink(diagnosticsSink)
    , m_bandConfigs(makeDefaultBandConfigs())
    , m_nextSampleIndex(m_config.firstSampleIndex)
{
}

SimulatorBcoSampleSource::~SimulatorBcoSampleSource()
{
    stop();
}

core::OperationResult SimulatorBcoSampleSource::start(SampleBatchCallback callback)
{
    if (!callback) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "BCO simulator sample source start rejected: callback is empty");
        return core::OperationResult::failure("sample batch callback must not be empty");
    }

    {
        std::lock_guard lock(m_mutex);
        if (m_running) {
            publish(infrastructure::DiagnosticSeverity::Warning,
                    "BCO simulator sample source start rejected: source is already running");
            return core::OperationResult::failure("BCO simulator sample source is already running");
        }

        m_stopRequested = false;
        m_running = true;
    }

    m_worker = std::thread(&SimulatorBcoSampleSource::generationLoop,
                           this,
                           std::move(callback));
    return core::OperationResult::ok();
}

core::OperationResult SimulatorBcoSampleSource::stop()
{
    std::thread worker;
    {
        std::lock_guard lock(m_mutex);
        if (!m_running && !m_worker.joinable()) {
            return core::OperationResult::ok();
        }

        m_stopRequested = true;
        worker = std::move(m_worker);
    }

    m_condition.notify_all();

    if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) {
        worker.join();
    }

    {
        std::lock_guard lock(m_mutex);
        m_running = false;
        m_stopRequested = false;
    }

    return core::OperationResult::ok();
}

void SimulatorBcoSampleSource::setBandConfigs(std::vector<core::BandConfig> configs)
{
    std::lock_guard lock(m_mutex);
    m_bandConfigs = std::move(configs);
}

std::vector<core::BandConfig> SimulatorBcoSampleSource::bandConfigs() const
{
    std::lock_guard lock(m_mutex);
    return m_bandConfigs;
}

BcoSampleBatch SimulatorBcoSampleSource::generateBatch()
{
    std::vector<core::BandConfig> configs;
    std::uint64_t nextSampleIndex = 0;
    std::uint64_t signalStep = 0;
    std::uint64_t sampleIndexStep = 1;
    std::size_t samplesPerBatch = 0;

    {
        std::lock_guard lock(m_mutex);
        configs = m_bandConfigs;
        nextSampleIndex = m_nextSampleIndex;
        signalStep = m_signalStep;
        sampleIndexStep = std::max<std::uint64_t>(1, m_config.sampleIndexStep);
        samplesPerBatch = m_config.samplesPerBatch;
    }

    BcoSampleBatch batch;
    if (configs.empty() || samplesPerBatch == 0) {
        return batch;
    }

    batch.samples.reserve(samplesPerBatch);

    std::size_t skippedDisabledConfigs = 0;
    while (batch.samples.size() < samplesPerBatch) {
        const auto& config = configs[static_cast<std::size_t>(signalStep % configs.size())];
        if (!config.enabled) {
            ++signalStep;
            ++skippedDisabledConfigs;
            if (skippedDisabledConfigs >= configs.size()) {
                break;
            }
            continue;
        }
        skippedDisabledConfigs = 0;

        const auto offsetHz = deterministicOffset(config, signalStep);
        const auto sampleIndex = nextSampleIndex;

        for (int beamIndex = 0; beamIndex < core::DomainConstraints::currentBeamCount; ++beamIndex) {
            if (batch.samples.size() >= samplesPerBatch) {
                break;
            }

            const core::BeamSample beamSample{
                sampleIndex,
                offsetHz,
                deterministicAmplitude(config.bandIndex, beamIndex, signalStep),
                beamIndex,
            };
            const auto sample = core::SignalSample::create(beamSample, config);
            if (sample) {
                batch.samples.push_back(*sample.value());
            }
        }

        nextSampleIndex += sampleIndexStep;
        ++signalStep;

    }

    {
        std::lock_guard lock(m_mutex);
        m_nextSampleIndex = nextSampleIndex;
        m_signalStep = signalStep;
    }

    return batch;
}

void SimulatorBcoSampleSource::generationLoop(SampleBatchCallback callback)
{
    for (;;) {
        {
            std::lock_guard lock(m_mutex);
            if (m_stopRequested) {
                break;
            }
        }

        auto batch = generateBatch();
        if (!batch.samples.empty()) {
            callback(batch);
        }

        std::unique_lock lock(m_mutex);
        if (m_condition.wait_for(lock, m_config.batchPeriod, [this] { return m_stopRequested; })) {
            break;
        }
    }
}

void SimulatorBcoSampleSource::publish(infrastructure::DiagnosticSeverity severity,
                                       const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SimulatorBcoSampleSource",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::hardware
