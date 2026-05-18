#include "hardware/simulator/simulator_bco_sample_source.h"

#include "hardware/simulator/simulator_antenna_state.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
    SimulatorRadioScene scene;
    std::uint64_t nextSampleIndex = 0;
    std::uint64_t signalStep = 0;
    std::uint64_t sampleIndexStep = 1;
    std::size_t samplesPerBatch = 0;
    int minVisibleAmplitude = 0;

    {
        std::lock_guard lock(m_mutex);
        configs = m_bandConfigs;
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
    const double antennaAzimuthDeg =
        m_antennaState ? m_antennaState->currentAzimuthDeg() : 0.0;

    std::size_t skippedSources = 0;
    while (batch.samples.size() < samplesPerBatch) {
        const auto& source =
            scene.sources[static_cast<std::size_t>(signalStep % scene.sources.size())];
        const auto absoluteFrequencyHz = sourceAbsoluteFrequency(source, signalStep);
        const auto* config = findBandContainingFrequency(configs, absoluteFrequencyHz);
        if (!config) {
            ++signalStep;
            ++skippedSources;
            if (skippedSources >= scene.sources.size()) {
                break;
            }
            continue;
        }

        const auto offsetHz = absoluteFrequencyHz - config->centerFrequencyHz;
        const auto sampleIndex = nextSampleIndex;
        const auto amplitudes =
            sourceBeamAmplitudes(source, antennaAzimuthDeg, minVisibleAmplitude);

        std::vector<core::SignalSample> generatedSamples;
        generatedSamples.reserve(kBeamCount);
        for (int beamIndex = 0; beamIndex < static_cast<int>(kBeamCount); ++beamIndex) {
            const auto amplitude = amplitudes[static_cast<std::size_t>(beamIndex)];
            if (!amplitude) {
                continue;
            }
            if (batch.samples.size() + generatedSamples.size() >= samplesPerBatch) {
                break;
            }

            const core::BeamSample beamSample{sampleIndex,
                                              offsetHz,
                                              *amplitude,
                                              beamIndex};
            const auto sample = core::SignalSample::create(beamSample, *config);
            if (sample) {
                generatedSamples.push_back(*sample.value());
            }
        }

        ++signalStep;
        if (generatedSamples.empty()) {
            ++skippedSources;
            if (skippedSources >= scene.sources.size()) {
                break;
            }
            continue;
        }

        skippedSources = 0;
        batch.samples.insert(batch.samples.end(), generatedSamples.begin(), generatedSamples.end());
        nextSampleIndex += sampleIndexStep;
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
