#include "app/scanbandresultaggregator.h"

#include "core/domain_constraints.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <utility>

namespace siriusscope::app {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kZeroVectorEpsilon = 1.0e-9;

bool validFrequency(std::int64_t frequencyHz) noexcept
{
    return frequencyHz >= core::DomainConstraints::minSystemFrequencyHz
           && frequencyHz <= core::DomainConstraints::maxSystemFrequencyHz;
}

bool validQuality(const std::optional<double>& quality) noexcept
{
    return quality && std::isfinite(*quality)
           && *quality >= core::DomainConstraints::minQuality
           && *quality <= core::DomainConstraints::maxQuality;
}

bool validAzimuth(double azimuthDeg) noexcept
{
    return std::isfinite(azimuthDeg)
           && azimuthDeg >= core::DomainConstraints::minAzimuthDeg
           && azimuthDeg < core::DomainConstraints::maxAzimuthDeg;
}

double toRadians(double degrees) noexcept
{
    return degrees * kPi / 180.0;
}

double toDegrees(double radians) noexcept
{
    return radians * 180.0 / kPi;
}

double normalizeAzimuth(double azimuthDeg) noexcept
{
    auto normalized = std::fmod(azimuthDeg, core::DomainConstraints::maxAzimuthDeg);
    if (normalized < core::DomainConstraints::minAzimuthDeg) {
        normalized += core::DomainConstraints::maxAzimuthDeg;
    }
    if (normalized >= core::DomainConstraints::maxAzimuthDeg) {
        return core::DomainConstraints::minAzimuthDeg;
    }
    return normalized;
}

double bearingWeight(const core::BearingResult& result) noexcept
{
    if (validQuality(result.quality) && *result.quality > 0.0) {
        return *result.quality;
    }
    return 1.0;
}

bool betterRepresentative(const core::BearingResult& candidate,
                          const core::BearingResult& current) noexcept
{
    const auto candidateHasQuality = validQuality(candidate.quality);
    const auto currentHasQuality = validQuality(current.quality);
    if (candidateHasQuality != currentHasQuality) {
        return candidateHasQuality;
    }
    if (candidateHasQuality && currentHasQuality && *candidate.quality != *current.quality) {
        return *candidate.quality > *current.quality;
    }
    if (candidate.resultTimeUtcNs != current.resultTimeUtcNs) {
        return candidate.resultTimeUtcNs > current.resultTimeUtcNs;
    }
    return candidate.sampleIndex > current.sampleIndex;
}

const core::BearingResult& representativeResult(
    const std::vector<const core::BearingResult*>& results)
{
    const auto* representative = results.front();
    for (const auto* result : results) {
        if (betterRepresentative(*result, *representative)) {
            representative = result;
        }
    }
    return *representative;
}

std::vector<std::int64_t> collectFrequencies(
    const std::vector<const core::BearingResult*>& results)
{
    std::vector<std::int64_t> frequenciesHz;
    for (const auto* result : results) {
        for (const auto frequencyHz : result->frequenciesHz) {
            if (validFrequency(frequencyHz)) {
                frequenciesHz.push_back(frequencyHz);
            }
        }
    }

    std::sort(frequenciesHz.begin(), frequenciesHz.end());
    frequenciesHz.erase(std::unique(frequenciesHz.begin(), frequenciesHz.end()),
                        frequenciesHz.end());
    return frequenciesHz;
}

std::vector<std::int64_t> collectRepresentativeFrequencies(
    const core::BearingResult& representative)
{
    std::vector<std::int64_t> frequenciesHz;
    for (const auto frequencyHz : representative.frequenciesHz) {
        if (validFrequency(frequencyHz)) {
            frequenciesHz.push_back(frequencyHz);
        }
    }

    std::sort(frequenciesHz.begin(), frequenciesHz.end());
    frequenciesHz.erase(std::unique(frequenciesHz.begin(), frequenciesHz.end()),
                        frequenciesHz.end());
    return frequenciesHz;
}

std::optional<double> maxQuality(const std::vector<const core::BearingResult*>& results)
{
    std::optional<double> quality;
    for (const auto* result : results) {
        if (!validQuality(result->quality)) {
            continue;
        }
        if (!quality || *result->quality > *quality) {
            quality = result->quality;
        }
    }
    return quality;
}

std::vector<core::ValidationIssue> collectDiagnostics(
    const std::vector<const core::BearingResult*>& results)
{
    std::vector<core::ValidationIssue> diagnostics;
    for (const auto* result : results) {
        diagnostics.insert(diagnostics.end(),
                           result->diagnostics.begin(),
                           result->diagnostics.end());
    }
    return diagnostics;
}

double aggregateBearing(const std::vector<const core::BearingResult*>& results,
                        const core::BearingResult& representative) noexcept
{
    double x = 0.0;
    double y = 0.0;
    for (const auto* result : results) {
        if (!validAzimuth(result->bearingAzimuthDeg)) {
            continue;
        }

        const auto radians = toRadians(result->bearingAzimuthDeg);
        const auto weight = bearingWeight(*result);
        x += weight * std::cos(radians);
        y += weight * std::sin(radians);
    }

    if (std::abs(x) <= kZeroVectorEpsilon && std::abs(y) <= kZeroVectorEpsilon) {
        return representative.bearingAzimuthDeg;
    }

    return normalizeAzimuth(toDegrees(std::atan2(y, x)));
}

std::optional<core::BearingResult> aggregateGroup(
    int bandIndex,
    const std::vector<const core::BearingResult*>& results)
{
    const auto& representative = representativeResult(results);
    auto frequenciesHz = collectFrequencies(results);
    if (frequenciesHz.empty()) {
        frequenciesHz = collectRepresentativeFrequencies(representative);
    }

    auto created = core::BearingResult::create(representative.sampleIndex,
                                               representative.resultTimeUtcNs,
                                               bandIndex,
                                               aggregateBearing(results, representative),
                                               frequenciesHz,
                                               maxQuality(results),
                                               collectDiagnostics(results),
                                               core::defaultRuntimeCapabilities());
    if (created) {
        return *created.value();
    }

    auto representativeFrequenciesHz = collectRepresentativeFrequencies(representative);
    if (representativeFrequenciesHz == frequenciesHz) {
        return std::nullopt;
    }

    created = core::BearingResult::create(representative.sampleIndex,
                                          representative.resultTimeUtcNs,
                                          bandIndex,
                                          representative.bearingAzimuthDeg,
                                          std::move(representativeFrequenciesHz),
                                          maxQuality(results),
                                          collectDiagnostics(results),
                                          core::defaultRuntimeCapabilities());
    if (!created) {
        return std::nullopt;
    }

    return *created.value();
}

} // namespace

std::vector<core::BearingResult> ScanBandResultAggregator::aggregateByBand(
    const std::vector<core::BearingResult>& results)
{
    std::map<int, std::vector<const core::BearingResult*>> groups;
    for (const auto& result : results) {
        groups[result.bandIndex].push_back(&result);
    }

    std::vector<core::BearingResult> aggregatedResults;
    aggregatedResults.reserve(groups.size());
    for (const auto& [bandIndex, groupResults] : groups) {
        if (auto aggregated = aggregateGroup(bandIndex, groupResults)) {
            aggregatedResults.push_back(std::move(*aggregated));
        }
    }

    return aggregatedResults;
}

} // namespace siriusscope::app
