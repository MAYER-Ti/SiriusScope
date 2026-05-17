#include "processing/bearing_service.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace siriusscope::processing {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

struct CandidateObservation
{
    int bandIndex = 0;
    std::uint64_t sampleIndex = 0;
    std::int64_t observedUtcNs = 0;
    double bearingDeg = 0.0;
    double score = 0.0;
    core::FrequencyRange frequencyRange;
};

ProcessingDiagnostic makeDiagnostic(ProcessingErrorCode code,
                                    ProcessingDiagnosticSeverity severity,
                                    std::string message)
{
    ProcessingDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = severity;
    diagnostic.message = std::move(message);
    return diagnostic;
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double normalizeAzimuthDeg(double deg)
{
    auto normalized = std::fmod(deg, core::DomainConstraints::maxAzimuthDeg);
    if (normalized < 0.0) {
        normalized += core::DomainConstraints::maxAzimuthDeg;
    }
    if (normalized >= core::DomainConstraints::maxAzimuthDeg) {
        normalized = 0.0;
    }
    return normalized;
}

std::optional<int> beamAmplitude(const BearingCandidate& candidate, int beamIndex)
{
    if (!candidate.hasBeam(beamIndex)
        || static_cast<std::size_t>(beamIndex) >= candidate.beamAmplitudes.size()) {
        return std::nullopt;
    }

    return candidate.beamAmplitudes[static_cast<std::size_t>(beamIndex)];
}

double beamDifference(int leftAmplitude, int rightAmplitude)
{
    const auto sum = leftAmplitude + rightAmplitude;
    if (sum <= 0) {
        return 0.0;
    }

    return static_cast<double>(leftAmplitude - rightAmplitude) / static_cast<double>(sum);
}

double candidateScore(int leftAmplitude, int rightAmplitude)
{
    const auto maxAmplitude = std::max(leftAmplitude, rightAmplitude);
    const auto amplitudeNorm = clamp01(
        static_cast<double>(maxAmplitude - core::DomainConstraints::minAmplitude)
        / static_cast<double>(core::DomainConstraints::maxAmplitude
                              - core::DomainConstraints::minAmplitude));
    const auto beamSumNorm = clamp01(
        static_cast<double>(leftAmplitude + rightAmplitude)
        / static_cast<double>(core::DomainConstraints::maxAmplitude
                              * core::DomainConstraints::currentBeamCount));

    return 0.7 * amplitudeNorm + 0.3 * beamSumNorm;
}

double estimatedBearingDeg(double antennaAzimuthDeg,
                           double difference,
                           const BearingServiceConfig& config)
{
    return normalizeAzimuthDeg(antennaAzimuthDeg - difference * config.beamHalfWidthDeg);
}

std::int64_t centerFrequencyHz(const core::FrequencyRange& range)
{
    return range.minHz + (range.maxHz - range.minHz) / 2;
}

std::vector<std::int64_t> uniqueSortedCenterFrequencies(
    const std::vector<CandidateObservation>& candidates)
{
    std::vector<std::int64_t> frequencies;
    frequencies.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        frequencies.push_back(centerFrequencyHz(candidate.frequencyRange));
    }

    std::sort(frequencies.begin(), frequencies.end());
    frequencies.erase(std::unique(frequencies.begin(), frequencies.end()), frequencies.end());
    return frequencies;
}

void appendDiagnostics(std::vector<ProcessingDiagnostic>& destination,
                       const std::vector<ProcessingDiagnostic>& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

void appendMissingBeamDiagnostic(BearingCalculationResult& result,
                                 const BearingCandidate& candidate,
                                 int beamIndex)
{
    auto diagnostic = makeDiagnostic(ProcessingErrorCode::MissingBeamSample,
                                     ProcessingDiagnosticSeverity::Warning,
                                     "bearing candidate lacks a required beam");
    diagnostic.bandIndex = candidate.bandIndex;
    diagnostic.sampleIndex = candidate.sampleIndexStart;
    diagnostic.beamIndex = beamIndex;
    diagnostic.frequencyBin = candidate.frequencyBin;
    diagnostic.frequencyHz = centerFrequencyHz(candidate.frequencyRange);
    result.diagnostics.push_back(std::move(diagnostic));
}

CandidateObservation makeCandidateObservation(const BearingFrameObservation& observation,
                                              const BearingCandidate& candidate,
                                              int leftAmplitude,
                                              int rightAmplitude,
                                              const BearingServiceConfig& config)
{
    const auto difference = beamDifference(leftAmplitude, rightAmplitude);
    return CandidateObservation{
        candidate.bandIndex,
        candidate.sampleIndexStart,
        observation.observedUtcNs,
        estimatedBearingDeg(observation.antennaAzimuthDeg, difference, config),
        candidateScore(leftAmplitude, rightAmplitude),
        candidate.frequencyRange,
    };
}

} // namespace

BearingService::BearingService(BearingServiceConfig config)
    : m_config(std::move(config))
{
}

BearingCalculationResult BearingService::calculate(
    const std::vector<BearingFrameObservation>& observations,
    const core::TimeBase& timeBase,
    const core::RuntimeCapabilities& capabilities) const
{
    BearingCalculationResult result;
    if (observations.empty()) {
        result.diagnostics.push_back(
            makeDiagnostic(ProcessingErrorCode::BearingNoObservations,
                           ProcessingDiagnosticSeverity::Warning,
                           "bearing calculation skipped: no observations"));
        return result;
    }

    std::map<int, std::vector<CandidateObservation>> candidatesByBand;
    for (const auto& observation : observations) {
        appendDiagnostics(result.diagnostics, observation.frame.diagnostics);

        for (const auto& candidate : observation.frame.candidates) {
            const auto left = beamAmplitude(candidate, m_config.leftBeamIndex);
            const auto right = beamAmplitude(candidate, m_config.rightBeamIndex);
            if (!left) {
                appendMissingBeamDiagnostic(result, candidate, m_config.leftBeamIndex);
                continue;
            }
            if (!right) {
                appendMissingBeamDiagnostic(result, candidate, m_config.rightBeamIndex);
                continue;
            }

            if (std::max(*left, *right) < m_config.minCandidateAmplitude) {
                continue;
            }

            candidatesByBand[candidate.bandIndex].push_back(
                makeCandidateObservation(observation, candidate, *left, *right, m_config));
        }
    }

    if (candidatesByBand.empty()) {
        result.diagnostics.push_back(
            makeDiagnostic(ProcessingErrorCode::BearingNoCandidates,
                           ProcessingDiagnosticSeverity::Warning,
                           "bearing calculation skipped: no complete two-beam candidates"));
        return result;
    }

    const auto topCount = std::max<std::size_t>(1, m_config.topObservationCount);
    for (auto& [bandIndex, bandCandidates] : candidatesByBand) {
        std::sort(bandCandidates.begin(),
                  bandCandidates.end(),
                  [](const auto& lhs, const auto& rhs) {
                      return lhs.score > rhs.score;
                  });
        if (bandCandidates.size() > topCount) {
            bandCandidates.resize(topCount);
        }

        double x = 0.0;
        double y = 0.0;
        double scoreSum = 0.0;
        double weightSum = 0.0;
        for (const auto& candidate : bandCandidates) {
            const auto angleRad = candidate.bearingDeg * kPi / 180.0;
            x += candidate.score * std::cos(angleRad);
            y += candidate.score * std::sin(angleRad);
            scoreSum += candidate.score;
            weightSum += candidate.score;
        }

        const auto quality = bandCandidates.empty()
            ? 0.0
            : clamp01(scoreSum / static_cast<double>(bandCandidates.size()));
        if (quality < m_config.minResultQuality || weightSum <= 0.0) {
            auto diagnostic = makeDiagnostic(ProcessingErrorCode::BearingQualityBelowThreshold,
                                             ProcessingDiagnosticSeverity::Warning,
                                             "bearing result quality is below threshold");
            diagnostic.bandIndex = bandIndex;
            result.diagnostics.push_back(std::move(diagnostic));
            continue;
        }

        auto bearingDeg = normalizeAzimuthDeg(std::atan2(y, x) * 180.0 / kPi);
        const auto& representative = bandCandidates.front();
        auto resultTimeUtcNs = representative.observedUtcNs;
        auto convertedTime = timeBase.globalTimeUtcNsForSample(representative.sampleIndex);
        if (convertedTime) {
            resultTimeUtcNs = *convertedTime.value();
        } else {
            auto diagnostic = makeDiagnostic(ProcessingErrorCode::BearingTimeConversionFailed,
                                             ProcessingDiagnosticSeverity::Warning,
                                             "timebase conversion failed for bearing result");
            diagnostic.bandIndex = bandIndex;
            diagnostic.sampleIndex = representative.sampleIndex;
            diagnostic.domainIssues = convertedTime.validation().issues();
            result.diagnostics.push_back(std::move(diagnostic));
        }

        auto created = core::BearingResult::create(
            representative.sampleIndex,
            resultTimeUtcNs,
            bandIndex,
            bearingDeg,
            uniqueSortedCenterFrequencies(bandCandidates),
            quality,
            {},
            capabilities);
        if (!created) {
            auto diagnostic = makeDiagnostic(ProcessingErrorCode::BearingResultRejected,
                                             ProcessingDiagnosticSeverity::Error,
                                             "bearing result rejected by domain validation");
            diagnostic.bandIndex = bandIndex;
            diagnostic.sampleIndex = representative.sampleIndex;
            diagnostic.domainIssues = created.validation().issues();
            result.diagnostics.push_back(std::move(diagnostic));
            continue;
        }

        result.results.push_back(*created.value());

        auto diagnostic = makeDiagnostic(ProcessingErrorCode::None,
                                         ProcessingDiagnosticSeverity::Info,
                                         "bearing result calculated");
        diagnostic.bandIndex = bandIndex;
        diagnostic.sampleIndex = representative.sampleIndex;
        result.diagnostics.push_back(std::move(diagnostic));
    }

    return result;
}

} // namespace siriusscope::processing
