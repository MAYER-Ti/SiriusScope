#include "core/domain_models.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace siriusscope::core {

namespace {

bool isFinite(double value)
{
    return std::isfinite(value);
}

ValidationResult validateCapabilities(const RuntimeCapabilities& capabilities)
{
    ValidationResult result;

    if (capabilities.bandCount <= 0) {
        result.add(ValidationCode::InvalidBandIndex, "bandCount must be positive");
    }

    if (capabilities.activeBeamCount <= 0
        || capabilities.activeBeamCount > capabilities.maxSupportedBeamCount
        || capabilities.maxSupportedBeamCount > DomainConstraints::futureMaxBeamCount) {
        result.add(ValidationCode::InvalidBeamIndex, "beam capability is outside supported range");
    }

    return result;
}

ValidationResult validateFrequencies(const std::vector<std::int64_t>& frequenciesHz)
{
    ValidationResult result;

    if (frequenciesHz.empty()) {
        result.add(ValidationCode::EmptyFrequencySet, "frequency set must not be empty");
        return result;
    }

    for (const auto frequencyHz : frequenciesHz) {
        result.merge(validateSystemFrequency(frequencyHz));
    }

    return result;
}

ValidationResult validateQuality(const std::optional<double>& quality)
{
    ValidationResult result;

    if (quality && (!isFinite(*quality) || *quality < DomainConstraints::minQuality
                    || *quality > DomainConstraints::maxQuality)) {
        result.add(ValidationCode::InvalidQuality, "quality must be in range 0..1");
    }

    return result;
}

} // namespace

std::int64_t FrequencyRange::widthHz() const noexcept
{
    return maxHz - minHz;
}

bool FrequencyRange::contains(std::int64_t frequencyHz) const noexcept
{
    return frequencyHz >= minHz && frequencyHz <= maxHz;
}

DomainResult<BandConfig> BandConfig::create(int bandIndex,
                                            std::int64_t centerFrequencyHz,
                                            std::int64_t widthHz,
                                            bool enabled,
                                            const RuntimeCapabilities& capabilities)
{
    BandConfig config{bandIndex, centerFrequencyHz, widthHz, enabled};
    auto validation = config.validate(capabilities);
    if (!validation) {
        return DomainResult<BandConfig>::failure(std::move(validation));
    }

    return DomainResult<BandConfig>::success(config);
}

FrequencyRange BandConfig::frequencyRange() const noexcept
{
    const auto halfWidthHz = widthHz / 2;
    return FrequencyRange{centerFrequencyHz - halfWidthHz, centerFrequencyHz + halfWidthHz};
}

bool BandConfig::containsFrequency(std::int64_t frequencyHz) const noexcept
{
    return frequencyRange().contains(frequencyHz);
}

ValidationResult BandConfig::validate(const RuntimeCapabilities& capabilities) const
{
    ValidationResult result;
    result.merge(validateCapabilities(capabilities));
    result.merge(validateBandIndex(bandIndex, capabilities));

    if (widthHz <= 0 || widthHz > DomainConstraints::maxBandWidthHz) {
        result.add(ValidationCode::InvalidBandWidth, "band width must be in range 1..500 MHz");
    }

    result.merge(validateSystemFrequency(centerFrequencyHz));

    const auto range = frequencyRange();
    if (range.minHz < DomainConstraints::minSystemFrequencyHz
        || range.maxHz > DomainConstraints::maxSystemFrequencyHz || range.minHz > range.maxHz) {
        result.add(ValidationCode::BandOutOfRange, "band range must stay inside 0.3..18 GHz");
    }

    return result;
}

DomainResult<SignalSample> SignalSample::create(const BeamSample& beamSample,
                                                const BandConfig& bandConfig,
                                                const RuntimeCapabilities& capabilities)
{
    SignalSample sample;
    sample.sampleIndex = beamSample.sampleIndex;
    sample.bandIndex = bandConfig.bandIndex;
    sample.frequencyOffsetHz = beamSample.frequencyOffsetHz;
    sample.absoluteFrequencyHz = bandConfig.centerFrequencyHz + beamSample.frequencyOffsetHz;
    sample.amplitude = beamSample.amplitude;
    sample.beamIndex = beamSample.beamIndex;

    auto validation = sample.validate(bandConfig, capabilities);
    if (!validation) {
        return DomainResult<SignalSample>::failure(std::move(validation));
    }

    return DomainResult<SignalSample>::success(sample);
}

ValidationResult SignalSample::validate(const BandConfig& bandConfig,
                                        const RuntimeCapabilities& capabilities) const
{
    ValidationResult result;
    result.merge(bandConfig.validate(capabilities));
    result.merge(validateBandIndex(bandIndex, capabilities));
    result.merge(validateAmplitude(amplitude));
    result.merge(validateBeamIndex(beamIndex, capabilities));

    if (bandIndex != bandConfig.bandIndex) {
        result.add(ValidationCode::InvalidBandIndex, "sample band does not match band config");
    }

    const auto maxOffsetHz = bandConfig.widthHz / 2;
    if (frequencyOffsetHz < -maxOffsetHz || frequencyOffsetHz > maxOffsetHz) {
        result.add(ValidationCode::InvalidFrequencyOffset, "frequency offset is outside band width");
    }

    const auto expectedFrequencyHz = bandConfig.centerFrequencyHz + frequencyOffsetHz;
    if (absoluteFrequencyHz != expectedFrequencyHz) {
        result.add(ValidationCode::InvalidFrequency, "absolute frequency does not match band center plus offset");
    }

    result.merge(validateSystemFrequency(absoluteFrequencyHz));
    if (!bandConfig.containsFrequency(absoluteFrequencyHz)) {
        result.add(ValidationCode::BandOutOfRange, "sample frequency is outside band range");
    }

    return result;
}

DomainResult<ScanSector> ScanSector::create(double startAzimuthDeg, double endAzimuthDeg)
{
    ScanSector sector{startAzimuthDeg, endAzimuthDeg};
    auto validation = sector.validate();
    if (!validation) {
        return DomainResult<ScanSector>::failure(std::move(validation));
    }

    return DomainResult<ScanSector>::success(sector);
}

ValidationResult ScanSector::validate() const
{
    ValidationResult result;
    result.merge(validateAzimuth(startAzimuthDeg));
    result.merge(validateAzimuth(endAzimuthDeg));

    if (result.isValid() && startAzimuthDeg == endAzimuthDeg) {
        result.add(ValidationCode::InvalidScanSector, "sector start and end must differ");
    }

    return result;
}

bool ScanSector::isWrapAround() const noexcept
{
    return startAzimuthDeg > endAzimuthDeg;
}

bool ScanSector::contains(double azimuthDeg) const noexcept
{
    if (!validateAzimuth(azimuthDeg)) {
        return false;
    }

    if (isWrapAround()) {
        return azimuthDeg >= startAzimuthDeg || azimuthDeg <= endAzimuthDeg;
    }

    return azimuthDeg >= startAzimuthDeg && azimuthDeg <= endAzimuthDeg;
}

double ScanSector::spanDegrees() const noexcept
{
    if (startAzimuthDeg == endAzimuthDeg) {
        return 0.0;
    }

    if (isWrapAround()) {
        return DomainConstraints::maxAzimuthDeg - startAzimuthDeg + endAzimuthDeg;
    }

    return endAzimuthDeg - startAzimuthDeg;
}

DomainResult<TimeBase> TimeBase::create(std::int64_t recordingStartUtcNs,
                                        std::uint64_t firstSampleIndex,
                                        std::uint64_t samplePeriodNs)
{
    TimeBase timeBase{recordingStartUtcNs, firstSampleIndex, samplePeriodNs};
    auto validation = timeBase.validate();
    if (!validation) {
        return DomainResult<TimeBase>::failure(std::move(validation));
    }

    return DomainResult<TimeBase>::success(timeBase);
}

ValidationResult TimeBase::validate() const
{
    ValidationResult result;

    if (recordingStartUtcNs < 0) {
        result.add(ValidationCode::InvalidTimeBase, "recording start UTC time must be non-negative");
    }

    if (samplePeriodNs == 0) {
        result.add(ValidationCode::InvalidTimeBase, "sample period must be positive");
    }

    return result;
}

DomainResult<std::int64_t> TimeBase::localTimeNsForSample(std::uint64_t sampleIndex) const
{
    auto validation = validate();
    if (sampleIndex < firstSampleIndex) {
        validation.add(ValidationCode::InvalidSampleIndex, "sample index is before first sample");
    }

    if (!validation) {
        return DomainResult<std::int64_t>::failure(std::move(validation));
    }

    const auto deltaSamples = sampleIndex - firstSampleIndex;
    const auto maxInt64 = static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max());
    if (samplePeriodNs != 0 && deltaSamples > maxInt64 / samplePeriodNs) {
        validation.add(ValidationCode::InvalidTimeBase, "local time conversion overflows int64");
        return DomainResult<std::int64_t>::failure(std::move(validation));
    }

    const auto localTimeNs = static_cast<std::int64_t>(deltaSamples * samplePeriodNs);
    return DomainResult<std::int64_t>::success(localTimeNs);
}

DomainResult<std::int64_t> TimeBase::globalTimeUtcNsForSample(std::uint64_t sampleIndex) const
{
    auto localTime = localTimeNsForSample(sampleIndex);
    if (!localTime) {
        return DomainResult<std::int64_t>::failure(localTime.validation());
    }

    const auto localNs = *localTime.value();
    ValidationResult validation;
    if (recordingStartUtcNs > std::numeric_limits<std::int64_t>::max() - localNs) {
        validation.add(ValidationCode::InvalidTimeBase, "global time conversion overflows int64");
        return DomainResult<std::int64_t>::failure(std::move(validation));
    }

    return DomainResult<std::int64_t>::success(recordingStartUtcNs + localNs);
}

DomainResult<BearingResult> BearingResult::create(std::uint64_t sampleIndex,
                                                  std::int64_t resultTimeUtcNs,
                                                  int bandIndex,
                                                  double bearingAzimuthDeg,
                                                  std::vector<std::int64_t> frequenciesHz,
                                                  std::optional<double> quality,
                                                  std::vector<ValidationIssue> diagnostics,
                                                  const RuntimeCapabilities& capabilities)
{
    BearingResult result{
        sampleIndex,
        resultTimeUtcNs,
        bandIndex,
        bearingAzimuthDeg,
        std::move(frequenciesHz),
        quality,
        std::move(diagnostics),
    };

    auto validation = result.validate(capabilities);
    if (!validation) {
        return DomainResult<BearingResult>::failure(std::move(validation));
    }

    return DomainResult<BearingResult>::success(std::move(result));
}

ValidationResult BearingResult::validate(const RuntimeCapabilities& capabilities) const
{
    ValidationResult result;
    result.merge(validateCapabilities(capabilities));
    result.merge(validateBandIndex(bandIndex, capabilities));
    result.merge(validateAzimuth(bearingAzimuthDeg));
    result.merge(validateFrequencies(frequenciesHz));
    result.merge(validateQuality(quality));

    if (resultTimeUtcNs < 0) {
        result.add(ValidationCode::InvalidTimeBase, "result UTC time must be non-negative");
    }

    return result;
}

DomainResult<ResultTableRow> ResultTableRow::fromBearingResult(
    const BearingResult& result,
    double antennaAzimuthDeg,
    const RuntimeCapabilities& capabilities)
{
    ResultTableRow row{
        result.sampleIndex,
        result.resultTimeUtcNs,
        antennaAzimuthDeg,
        result.bandIndex,
        result.frequenciesHz,
        result.quality,
        result.diagnostics,
    };

    auto validation = row.validate(capabilities);
    if (!validation) {
        return DomainResult<ResultTableRow>::failure(std::move(validation));
    }

    return DomainResult<ResultTableRow>::success(std::move(row));
}

ValidationResult ResultTableRow::validate(const RuntimeCapabilities& capabilities) const
{
    ValidationResult result;
    result.merge(validateCapabilities(capabilities));
    result.merge(validateBandIndex(bandIndex, capabilities));
    result.merge(validateAzimuth(antennaAzimuthDeg));
    result.merge(validateFrequencies(frequenciesHz));
    result.merge(validateQuality(quality));

    if (resultTimeUtcNs < 0) {
        result.add(ValidationCode::InvalidTimeBase, "row UTC time must be non-negative");
    }

    return result;
}

ValidationResult validateAmplitude(int amplitude)
{
    if (amplitude < DomainConstraints::minAmplitude || amplitude > DomainConstraints::maxAmplitude) {
        return ValidationResult::invalid(ValidationCode::InvalidAmplitude,
                                         "amplitude must be in range 1..127");
    }

    return ValidationResult::ok();
}

ValidationResult validateBeamIndex(int beamIndex, const RuntimeCapabilities& capabilities)
{
    auto result = validateCapabilities(capabilities);
    if (beamIndex < 0 || beamIndex >= capabilities.activeBeamCount) {
        result.add(ValidationCode::InvalidBeamIndex, "beam index is outside active beam range");
    }

    return result;
}

ValidationResult validateBandIndex(int bandIndex, const RuntimeCapabilities& capabilities)
{
    auto result = validateCapabilities(capabilities);
    if (bandIndex < 0 || bandIndex >= capabilities.bandCount) {
        result.add(ValidationCode::InvalidBandIndex, "band index is outside configured band range");
    }

    return result;
}

ValidationResult validateAzimuth(double azimuthDeg)
{
    if (!isFinite(azimuthDeg) || azimuthDeg < DomainConstraints::minAzimuthDeg
        || azimuthDeg >= DomainConstraints::maxAzimuthDeg) {
        return ValidationResult::invalid(ValidationCode::InvalidAzimuth,
                                         "azimuth must be in range [0, 360)");
    }

    return ValidationResult::ok();
}

ValidationResult validateSystemFrequency(std::int64_t frequencyHz)
{
    if (frequencyHz < DomainConstraints::minSystemFrequencyHz
        || frequencyHz > DomainConstraints::maxSystemFrequencyHz) {
        return ValidationResult::invalid(ValidationCode::InvalidFrequency,
                                         "frequency must be in range 0.3..18 GHz");
    }

    return ValidationResult::ok();
}

} // namespace siriusscope::core
