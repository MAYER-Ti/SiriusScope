#include "bco_generator/generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace bco_generator {
namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kBeamHalfSeparationDeg = 30.0;
constexpr std::size_t kBeamCount = 2;
struct SampleBuffer {
    std::span<Sample> storage;
    std::size_t count = 0;
    std::size_t size() const noexcept { return count; }
    void push_back(Sample sample) noexcept { storage[count++] = sample; }
};
struct Block { SampleBuffer samples; };

std::uint64_t saturatedAdd(std::uint64_t value, std::uint64_t increment)
{
    if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
        return std::numeric_limits<std::uint64_t>::max();
    }

    return value + increment;
}

const PulseConfig* pulseConfigForBand(
    std::span<const PulseConfig> configs,
    int bandIndex)
{
    const auto found = std::find_if(configs.begin(), configs.end(), [bandIndex](const auto& item) {
        return item.bandIndex == bandIndex;
    });
    return found == configs.end() ? nullptr : &(*found);
}

bool isInsidePulse(std::uint64_t sampleIndex,
                   std::uint64_t firstSampleIndex,
                   std::uint64_t samplePeriodNs,
                   const PulseConfig& config)
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
                                        const PulseConfig& config)
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

const Band* findBandContainingFrequency(
    std::span<const Band> configs,
    std::int64_t absoluteFrequencyHz)
{
    const auto found =
        std::find_if(configs.begin(), configs.end(), [absoluteFrequencyHz](const auto& config) {
            return config.containsFrequency(absoluteFrequencyHz);
        });
    return found == configs.end() ? nullptr : &(*found);
}

double normalize360(double value)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }

    auto normalized = std::fmod(value, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    if (normalized >= 360.0) {
        normalized = 0.0;
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
    if (amplitude < 1) {
        return std::nullopt;
    }

    const int configuredThreshold = std::clamp(minVisibleAmplitude,
                                               0,
                                               127);
    if (configuredThreshold > 0 && amplitude < configuredThreshold) {
        return std::nullopt;
    }

    return std::clamp(amplitude,
                      1,
                      127);
}

std::array<std::optional<int>, kBeamCount> sourceBeamAmplitudes(
    const RadioSource& source,
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

std::int64_t sourceAbsoluteFrequency(const RadioSource& source,
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
    std::span<const Band> enabledBands,
    std::span<const PulseConfig> pulseConfigs)
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

bool pulseAllowsBandSample(std::uint64_t sampleIndex,
                           std::uint64_t timeBaseFirstSampleIndex,
                           std::uint64_t samplePeriodNs,
                           int bandIndex,
                           std::span<const PulseConfig> pulseConfigs)
{
    const auto* pulseConfig = pulseConfigForBand(pulseConfigs, bandIndex);
    if (!pulseConfig) {
        return true;
    }
    if (!pulseConfig->enabled) {
        return false;
    }
    if (!hasValidPulseTiming(*pulseConfig)) {
        return true;
    }

    return isInsidePulse(sampleIndex,
                         timeBaseFirstSampleIndex,
                         samplePeriodNs,
                         *pulseConfig);
}

void appendFastContinuousSamples(Block& block, std::uint64_t first,
                                 std::span<const Band> bands,
                                 std::span<const RadioSource> scene,
                                 double azimuth, int threshold, std::span<Sample> scratch)
{
    std::size_t count = 0;
    for (const auto& source : scene) {
        const auto* band = findBandContainingFrequency(bands, source.absoluteFrequencyHz);
        if (!band) continue;
        const auto amplitudes = sourceBeamAmplitudes(source, azimuth, threshold);
        for (int beam = 0; beam < 2; ++beam) {
            if (!amplitudes[beam]) continue;
            scratch[count++] = {0, band->bandIndex,
                source.absoluteFrequencyHz - band->centerFrequencyHz,
                source.absoluteFrequencyHz, *amplitudes[beam], beam};
        }
    }
    if (count == 0) return;
    std::size_t templateIndex = 0;
    for (std::size_t i = 0; i < block.samples.storage.size(); ++i) {
        auto sample = scratch[templateIndex];
        sample.sampleIndex = saturatedAdd(first, i);
        block.samples.push_back(sample);
        if (++templateIndex == count) templateIndex = 0;
    }
}

std::size_t appendSourceSamples(Block& block,
                                std::size_t maxSampleCount,
                                const Band& band,
                                const RadioSource& source,
                                std::int64_t absoluteFrequencyHz,
                                std::uint64_t sampleIndex,
                                double antennaAzimuthDeg,
                                int minVisibleAmplitude)
{
    const auto offsetHz = absoluteFrequencyHz - band.centerFrequencyHz;
    const auto amplitudes =
        sourceBeamAmplitudes(source, antennaAzimuthDeg, minVisibleAmplitude);

    std::size_t appended = 0;
    for (int beamIndex = 0; beamIndex < static_cast<int>(kBeamCount); ++beamIndex) {
        if (block.samples.size() >= maxSampleCount) {
            break;
        }

        const auto amplitude = amplitudes[static_cast<std::size_t>(beamIndex)];
        if (!amplitude) {
            continue;
        }

        if (band.valid) {
            block.samples.push_back({sampleIndex, band.bandIndex, offsetHz,
                                     absoluteFrequencyHz, *amplitude, beamIndex});
            ++appended;
        }
    }
    return appended;
}


} // namespace

bool hasValidPulseTiming(const PulseConfig& config) noexcept
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

BatchResult generate(const BatchRequest& request, std::span<Sample> output,
                     std::span<Sample> scratch) noexcept
{
    const auto enabledBands = request.bands;
    const auto scene = request.sources;
    const auto pulseConfigs = request.pulses;
    const auto batchStartSampleIndex = request.firstSampleIndex;
    const auto timeBaseFirstSampleIndex = request.timeBaseFirstSampleIndex;
    const auto samplePeriodNs = std::max<std::uint64_t>(1, request.samplePeriodNs);
    const auto sampleCount = output.size();
    const auto sampleSlotsInBatch = static_cast<std::uint64_t>(sampleCount);
    const auto batchEndSampleIndex = saturatedAdd(batchStartSampleIndex, sampleSlotsInBatch);
    const auto antennaAzimuthDeg = request.antennaAzimuthDeg;
    const auto minVisibleAmplitude = request.minVisibleAmplitude;
    const bool fast = request.fastContinuous && pulseConfigs.empty();
    if (fast && scene.size() > scratch.size() / 2) {
        return {Status::InsufficientScratch, 0, batchStartSampleIndex};
    }
    Block block{{output, 0}};
    if (fast && !enabledBands.empty() && !scene.empty() && sampleSlotsInBatch > 0) {
        appendFastContinuousSamples(block, batchStartSampleIndex, enabledBands, scene,
                                    antennaAzimuthDeg, minVisibleAmplitude, scratch);
    } else if (!enabledBands.empty() && !scene.empty() && sampleSlotsInBatch > 0) {
        auto sampleIndex = batchStartSampleIndex;
        while (sampleIndex < batchEndSampleIndex && block.samples.size() < sampleCount) {
            if (!pulseConfigs.empty()) {
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
            }

            bool generatedAtSampleIndex = false;
            const auto signalStep = sampleIndex >= timeBaseFirstSampleIndex
                ? sampleIndex - timeBaseFirstSampleIndex
                : 0;
            for (const auto& source : scene) {
                if (block.samples.size() >= sampleCount) {
                    break;
                }

                const auto absoluteFrequencyHz = sourceAbsoluteFrequency(source, signalStep);
                const auto* band =
                    findBandContainingFrequency(enabledBands, absoluteFrequencyHz);
                if (!band) {
                    continue;
                }
                if (!pulseAllowsBandSample(sampleIndex,
                                           timeBaseFirstSampleIndex,
                                           samplePeriodNs,
                                           band->bandIndex,
                                           pulseConfigs)) {
                    continue;
                }

                const auto appended = appendSourceSamples(block,
                                                          sampleCount,
                                                          *band,
                                                          source,
                                                          absoluteFrequencyHz,
                                                          sampleIndex,
                                                          antennaAzimuthDeg,
                                                          minVisibleAmplitude);
                generatedAtSampleIndex = generatedAtSampleIndex || appended > 0;
            }

            sampleIndex = saturatedAdd(sampleIndex, 1);
            if (!generatedAtSampleIndex && sampleIndex == std::numeric_limits<std::uint64_t>::max()) {
                break;
            }
        }
    }

    return {Status::Ok, block.samples.size(), batchEndSampleIndex};
}

} // namespace bco_generator
