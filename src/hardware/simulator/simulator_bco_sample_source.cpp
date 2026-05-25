#include "hardware/simulator/simulator_bco_sample_source.h"

#include "hardware/simulator/simulator_antenna_state.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
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

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kBeamHalfSeparationDeg = 30.0;

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

std::vector<SimulatorPulseBandConfig> makeDefaultPulseBandConfigs()
{
    std::vector<SimulatorPulseBandConfig> configs;
    configs.reserve(core::DomainConstraints::currentBandCount);

    for (int bandIndex = 0; bandIndex < core::DomainConstraints::currentBandCount; ++bandIndex) {
        configs.push_back(SimulatorPulseBandConfig{bandIndex, true, 100000.0, 10000.0});
    }

    return configs;
}

const core::BandConfig* findBandContainingFrequency(
    const std::vector<core::BandConfig>& configs,
    std::int64_t absoluteFrequencyHz)
{
    const auto found =
        std::find_if(configs.begin(), configs.end(), [absoluteFrequencyHz](const auto& config) {
            return config.enabled && config.containsFrequency(absoluteFrequencyHz);
        });
    return found == configs.end() ? nullptr : &(*found);
}

const SimulatorPulseBandConfig* findPulseConfigForBand(
    const std::vector<SimulatorPulseBandConfig>& configs,
    int bandIndex)
{
    const auto found = std::find_if(configs.begin(), configs.end(), [bandIndex](const auto& config) {
        return config.bandIndex == bandIndex;
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

long double pulsePhaseNs(std::uint64_t sampleIndex,
                         std::uint64_t samplePeriodNs,
                         const SimulatorPulseBandConfig& config)
{
    const long double periodNs = static_cast<long double>(config.pulsePeriodUs) * 1000.0L;
    auto phaseNs = std::fmod(
        static_cast<long double>(sampleIndex) * static_cast<long double>(samplePeriodNs),
        periodNs);
    if (phaseNs < 0.0L) {
        phaseNs += periodNs;
    }
    return phaseNs;
}

bool isInsidePulseWindow(std::uint64_t sampleIndex,
                         std::uint64_t samplePeriodNs,
                         const SimulatorPulseBandConfig& config)
{
    if (!hasValidPulseTiming(config)) {
        return false;
    }

    const long double widthNs = static_cast<long double>(config.pulseWidthUs) * 1000.0L;
    const auto phaseNs = pulsePhaseNs(sampleIndex, samplePeriodNs, config);
    return phaseNs >= 0.0L && phaseNs < widthNs;
}

std::optional<std::uint64_t> nextPulseStartSampleIndex(
    std::uint64_t currentSampleIndex,
    std::uint64_t samplePeriodNs,
    const std::vector<SimulatorPulseBandConfig>& pulseConfigs)
{
    std::optional<std::uint64_t> nearest;
    const auto safeSamplePeriodNs = std::max<std::uint64_t>(1, samplePeriodNs);
    const long double currentTimeNs =
        static_cast<long double>(currentSampleIndex)
        * static_cast<long double>(safeSamplePeriodNs);

    for (const auto& config : pulseConfigs) {
        if (!hasValidPulseTiming(config)) {
            continue;
        }
        if (isInsidePulseWindow(currentSampleIndex, safeSamplePeriodNs, config)) {
            return currentSampleIndex;
        }

        const long double periodNs = static_cast<long double>(config.pulsePeriodUs) * 1000.0L;
        const auto phaseNs = pulsePhaseNs(currentSampleIndex, safeSamplePeriodNs, config);
        const long double nextStartTimeNs = currentTimeNs + periodNs - phaseNs;
        const long double candidateValue =
            std::ceil(nextStartTimeNs / static_cast<long double>(safeSamplePeriodNs));
        if (candidateValue > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
            continue;
        }

        auto candidate = static_cast<std::uint64_t>(candidateValue);
        if (candidate <= currentSampleIndex) {
            candidate = currentSampleIndex + 1;
        }

        if (!nearest || candidate < *nearest) {
            nearest = candidate;
        }
    }

    return nearest;
}

double normalize360(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }

    auto normalized = std::fmod(value, core::DomainConstraints::maxAzimuthDeg);
    if (normalized < core::DomainConstraints::minAzimuthDeg) {
        normalized += core::DomainConstraints::maxAzimuthDeg;
    }
    if (normalized >= core::DomainConstraints::maxAzimuthDeg) {
        normalized = core::DomainConstraints::minAzimuthDeg;
    }

    return normalized;
}

double signedAngularDeltaDeg(double fromDeg, double toDeg)
{
    double delta = normalize360(toDeg) - normalize360(fromDeg);
    if (delta > 180.0) {
        delta -= 360.0;
    }
    if (delta < -180.0) {
        delta += 360.0;
    }

    return delta;
}

double beamGain(double deltaDeg, double sigmaDeg)
{
    if (!std::isfinite(sigmaDeg) || sigmaDeg <= 0.0) {
        return 0.0;
    }

    const double x = deltaDeg / sigmaDeg;
    return std::exp(-0.5 * x * x);
}

std::optional<int> toVisibleBcoAmplitude(double value, int minVisibleAmplitude)
{
    if (!std::isfinite(value)) {
        return std::nullopt;
    }

    const int amplitude = static_cast<int>(std::lround(value));
    const int threshold = std::clamp(minVisibleAmplitude,
                                     core::DomainConstraints::minAmplitude,
                                     core::DomainConstraints::maxAmplitude);
    if (amplitude < threshold) {
        return std::nullopt;
    }

    return std::clamp(amplitude,
                      core::DomainConstraints::minAmplitude,
                      core::DomainConstraints::maxAmplitude);
}

std::array<std::optional<int>, 2> sourceBeamAmplitudes(const SimulatedRadioSource& source,
                                                       double antennaAzimuthDeg,
                                                       int minVisibleAmplitude)
{
    const double beam0Axis = antennaAzimuthDeg - kBeamHalfSeparationDeg;
    const double beam1Axis = antennaAzimuthDeg + kBeamHalfSeparationDeg;
    const double delta0 = signedAngularDeltaDeg(beam0Axis, source.azimuthDeg);
    const double delta1 = signedAngularDeltaDeg(beam1Axis, source.azimuthDeg);
    const double peakAmplitude = static_cast<double>(source.peakAmplitude);

    return {
        toVisibleBcoAmplitude(peakAmplitude * beamGain(delta0, source.beamSigmaDeg),
                              minVisibleAmplitude),
        toVisibleBcoAmplitude(peakAmplitude * beamGain(delta1, source.beamSigmaDeg),
                              minVisibleAmplitude),
    };
}

std::int64_t sourceAbsoluteFrequency(const SimulatedRadioSource& source,
                                     std::uint64_t signalStep)
{
    if (!source.frequencyDriftEnabled || source.driftPeriodSteps == 0) {
        return source.absoluteFrequencyHz;
    }

    const double phase =
        2.0 * kPi * static_cast<double>(signalStep % source.driftPeriodSteps)
        / static_cast<double>(source.driftPeriodSteps);
    return source.absoluteFrequencyHz
        + static_cast<std::int64_t>(
            std::llround(std::sin(phase) * static_cast<double>(source.driftSpanHz)));
}

std::uint64_t modelStepCount(std::uint64_t fromSampleIndex,
                             std::uint64_t toSampleIndex,
                             std::uint64_t sampleIndexStep)
{
    if (toSampleIndex <= fromSampleIndex) {
        return 1;
    }

    const auto delta = toSampleIndex - fromSampleIndex;
    return ((delta - 1) / std::max<std::uint64_t>(1, sampleIndexStep)) + 1;
}

} // namespace

SimulatorBcoSampleSource::SimulatorBcoSampleSource(
    SimulatorBcoSampleSourceConfig config,
    SimulatorAntennaState* antennaState,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_config(std::move(config))
    , m_antennaState(antennaState)
    , m_diagnosticsSink(diagnosticsSink)
    , m_scene(makeDefaultSimulatorRadioScene())
    , m_bandConfigs(makeDefaultBandConfigs())
    , m_pulseBandConfigs(makeDefaultPulseBandConfigs())
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

void SimulatorBcoSampleSource::setPulseBandConfigs(std::vector<SimulatorPulseBandConfig> configs)
{
    std::lock_guard lock(m_mutex);
    m_pulseBandConfigs = std::move(configs);
}

std::vector<SimulatorPulseBandConfig> SimulatorBcoSampleSource::pulseBandConfigs() const
{
    std::lock_guard lock(m_mutex);
    return m_pulseBandConfigs;
}

void SimulatorBcoSampleSource::resetSession(std::uint64_t firstSampleIndex)
{
    std::lock_guard lock(m_mutex);
    m_nextSampleIndex = firstSampleIndex;
    m_signalStep = 0;
}

BcoSampleBatch SimulatorBcoSampleSource::generateBatch()
{
    std::vector<core::BandConfig> configs;
    std::vector<SimulatorPulseBandConfig> pulseConfigs;
    SimulatorRadioScene scene;
    std::uint64_t nextSampleIndex = 0;
    std::uint64_t signalStep = 0;
    std::uint64_t sampleIndexStep = 1;
    std::size_t samplesPerBatch = 0;
    int minVisibleAmplitude = 0;

    {
        std::lock_guard lock(m_mutex);
        configs = m_bandConfigs;
        pulseConfigs = m_pulseBandConfigs;
        scene = m_scene;
        nextSampleIndex = m_nextSampleIndex;
        signalStep = m_signalStep;
        sampleIndexStep = std::max<std::uint64_t>(1, m_config.sampleIndexStep);
        samplesPerBatch = m_config.samplesPerBatch;
        minVisibleAmplitude = m_config.minVisibleAmplitude;
    }

    BcoSampleBatch batch;
    constexpr std::size_t kBeamCount = 2;
    if (configs.empty() || scene.sources.empty() || samplesPerBatch == 0) {
        return batch;
    }

    batch.samples.reserve(samplesPerBatch);
    const std::size_t maxAttempts = std::max(samplesPerBatch * 20, scene.sources.size() * 20);
    const double antennaAzimuthDeg =
        m_antennaState ? m_antennaState->currentAzimuthDeg() : 0.0;

    const auto samplePeriodNs = core::DomainConstraints::defaultSamplePeriodNs;
    std::size_t attempts = 0;
    while (batch.samples.size() < samplesPerBatch && attempts < maxAttempts) {
        ++attempts;
        const auto sampleIndex = nextSampleIndex;
        bool generatedAtSampleIndex = false;

        for (const auto& source : scene.sources) {
            if (batch.samples.size() >= samplesPerBatch) {
                break;
            }

            const auto absoluteFrequencyHz = sourceAbsoluteFrequency(source, signalStep);
            const auto* config = findBandContainingFrequency(configs, absoluteFrequencyHz);
            if (!config) {
                continue;
            }

            const auto* pulseConfig = findPulseConfigForBand(pulseConfigs, config->bandIndex);
            if (!pulseConfig || !isInsidePulseWindow(sampleIndex, samplePeriodNs, *pulseConfig)) {
                continue;
            }

            const auto offsetHz = absoluteFrequencyHz - config->centerFrequencyHz;
            const auto amplitudes =
                sourceBeamAmplitudes(source, antennaAzimuthDeg, minVisibleAmplitude);

            for (int beamIndex = 0; beamIndex < static_cast<int>(kBeamCount); ++beamIndex) {
                if (batch.samples.size() >= samplesPerBatch) {
                    break;
                }

                const auto amplitude = amplitudes[static_cast<std::size_t>(beamIndex)];
                if (!amplitude) {
                    continue;
                }

                const core::BeamSample beamSample{sampleIndex,
                                                  offsetHz,
                                                  *amplitude,
                                                  beamIndex};
                const auto sample = core::SignalSample::create(beamSample, *config);
                if (sample) {
                    batch.samples.push_back(*sample.value());
                    generatedAtSampleIndex = true;
                }
            }
        }

        if (generatedAtSampleIndex) {
            nextSampleIndex += sampleIndexStep;
            ++signalStep;
            continue;
        }

        const auto nextPulseStart =
            nextPulseStartSampleIndex(sampleIndex, samplePeriodNs, pulseConfigs);
        if (nextPulseStart && *nextPulseStart > sampleIndex) {
            signalStep += modelStepCount(sampleIndex, *nextPulseStart, sampleIndexStep);
            nextSampleIndex = *nextPulseStart;
        } else {
            nextSampleIndex += sampleIndexStep;
            ++signalStep;
        }
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
