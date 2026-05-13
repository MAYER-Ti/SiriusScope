#pragma once

/*!
 * \file domain_models.h
 * \brief Core SiriusScope domain models and validation entry points.
 */

#include "core/domain_constraints.h"
#include "core/domain_validation.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::core {

/*!
 * \brief Inclusive frequency interval in hertz.
 */
struct FrequencyRange
{
    //! Lower frequency edge, in hertz.
    std::int64_t minHz = 0;
    //! Upper frequency edge, in hertz.
    std::int64_t maxHz = 0;

    /*!
     * \brief Returns the interval width.
     *
     * \return maxHz - minHz. Invalid ranges can therefore produce a negative value.
     */
    std::int64_t widthHz() const noexcept;
    /*!
     * \brief Checks whether a frequency belongs to this inclusive range.
     *
     * \param[in] frequencyHz Frequency in hertz.
     * \return true when frequencyHz is inside [minHz, maxHz].
     */
    bool contains(std::int64_t frequencyHz) const noexcept;
};

/*!
 * \brief Configuration of one BCO band represented by a BandItem.
 *
 * The model is UI-independent. UI changes must be validated by application
 * controllers before they reach hardware command adapters.
 */
struct BandConfig
{
    //! Zero-based band index.
    int bandIndex = 0;
    //! Band center frequency, in hertz.
    std::int64_t centerFrequencyHz = DomainConstraints::minSystemFrequencyHz;
    //! Band width in hertz. Current maximum is 500 MHz.
    std::int64_t widthHz = DomainConstraints::maxBandWidthHz;
    //! Whether the band participates in reception and visualization.
    bool enabled = true;

    /*!
     * \brief Creates a validated band configuration.
     *
     * \param[in] bandIndex Zero-based band index.
     * \param[in] centerFrequencyHz Band center frequency, in hertz.
     * \param[in] widthHz Band width, in hertz.
     * \param[in] enabled Whether the band is enabled.
     * \param[in] capabilities Runtime band and beam limits.
     * \return Created config or validation issues.
     */
    static DomainResult<BandConfig> create(int bandIndex,
                                           std::int64_t centerFrequencyHz,
                                           std::int64_t widthHz,
                                           bool enabled = true,
                                           const RuntimeCapabilities& capabilities =
                                               defaultRuntimeCapabilities());

    /*!
     * \brief Returns the inclusive frequency range covered by the band.
     *
     * \return Center frequency plus/minus half the configured width.
     */
    FrequencyRange frequencyRange() const noexcept;
    /*!
     * \brief Checks whether an absolute frequency is inside this band.
     *
     * \param[in] frequencyHz Absolute frequency in hertz.
     * \return true when the frequency belongs to frequencyRange().
     */
    bool containsFrequency(std::int64_t frequencyHz) const noexcept;
    /*!
     * \brief Validates band index, center frequency, width, and system bounds.
     *
     * \param[in] capabilities Runtime band and beam limits.
     * \return Validation result with all detected issues.
     */
    ValidationResult validate(const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Raw beam-local sample before it is attached to a band.
 */
struct BeamSample
{
    //! Original BCO sample index. It must be preserved by all higher layers.
    std::uint64_t sampleIndex = 0;
    //! Frequency offset from the band center, in hertz.
    std::int64_t frequencyOffsetHz = 0;
    //! Input amplitude. Valid range is 1..127.
    int amplitude = 0;
    //! Beam index from the current antenna model.
    int beamIndex = 0;
};

/*!
 * \brief Validated signal sample with absolute frequency and band context.
 */
struct SignalSample
{
    //! Original BCO sample index.
    std::uint64_t sampleIndex = 0;
    //! Zero-based band index.
    int bandIndex = 0;
    //! Frequency offset from the band center, in hertz.
    std::int64_t frequencyOffsetHz = 0;
    //! Absolute sample frequency in hertz.
    std::int64_t absoluteFrequencyHz = 0;
    //! Input amplitude in the valid 1..127 range.
    int amplitude = 0;
    //! Beam index in the current runtime capabilities.
    int beamIndex = 0;

    /*!
     * \brief Creates a validated signal sample from a beam sample and band config.
     *
     * \param[in] beamSample Beam-local sample received from parser or simulator.
     * \param[in] bandConfig Band used to derive the absolute frequency.
     * \param[in] capabilities Runtime band and beam limits.
     * \return Created sample or validation issues.
     */
    static DomainResult<SignalSample> create(const BeamSample& beamSample,
                                             const BandConfig& bandConfig,
                                             const RuntimeCapabilities& capabilities =
                                                 defaultRuntimeCapabilities());

    /*!
     * \brief Validates sample fields against the related band configuration.
     *
     * \param[in] bandConfig Band expected to own this sample.
     * \param[in] capabilities Runtime band and beam limits.
     * \return Validation result with all detected issues.
     */
    ValidationResult validate(const BandConfig& bandConfig,
                              const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Antenna scan sector in degrees.
 *
 * The range uses [0, 360) azimuth values. A sector wraps around north when
 * startAzimuthDeg is greater than endAzimuthDeg.
 */
struct ScanSector
{
    //! Sector start azimuth in degrees, inclusive.
    double startAzimuthDeg = 0.0;
    //! Sector end azimuth in degrees, inclusive for contains().
    double endAzimuthDeg = 0.0;

    /*!
     * \brief Creates a validated scan sector.
     *
     * \param[in] startAzimuthDeg Sector start in degrees.
     * \param[in] endAzimuthDeg Sector end in degrees.
     * \return Created sector or validation issues.
     */
    static DomainResult<ScanSector> create(double startAzimuthDeg, double endAzimuthDeg);

    /*!
     * \brief Validates azimuth bounds and non-zero span.
     *
     * \return Validation result.
     */
    ValidationResult validate() const;
    /*!
     * \brief Checks whether the sector crosses the 360/0 degree boundary.
     *
     * \return true when startAzimuthDeg > endAzimuthDeg.
     */
    bool isWrapAround() const noexcept;
    /*!
     * \brief Checks whether an azimuth belongs to this sector.
     *
     * \param[in] azimuthDeg Azimuth in degrees.
     * \return true for azimuths inside the sector.
     */
    bool contains(double azimuthDeg) const noexcept;
    /*!
     * \brief Calculates sector span in degrees.
     *
     * \return Sector span, accounting for wrap-around.
     */
    double spanDegrees() const noexcept;
};

/*!
 * \brief Converts preserved BCO sample indices to local and UTC time.
 */
struct TimeBase
{
    //! UTC recording start timestamp in nanoseconds.
    std::int64_t recordingStartUtcNs = 0;
    //! First BCO sample index in the recording.
    std::uint64_t firstSampleIndex = 0;
    //! Duration of one sample step in nanoseconds.
    std::uint64_t samplePeriodNs = DomainConstraints::defaultSamplePeriodNs;

    /*!
     * \brief Creates a validated time base.
     *
     * \param[in] recordingStartUtcNs UTC recording start timestamp in nanoseconds.
     * \param[in] firstSampleIndex First BCO sample index in the recording.
     * \param[in] samplePeriodNs Duration of one sample step in nanoseconds.
     * \return Created time base or validation issues.
     */
    static DomainResult<TimeBase> create(std::int64_t recordingStartUtcNs,
                                         std::uint64_t firstSampleIndex,
                                         std::uint64_t samplePeriodNs);

    /*!
     * \brief Validates timestamp origin and sample period.
     *
     * \return Validation result.
     */
    ValidationResult validate() const;
    /*!
     * \brief Converts a sample index to time from recording start.
     *
     * \param[in] sampleIndex Original BCO sample index.
     * \return Local time in nanoseconds or validation issues.
     */
    DomainResult<std::int64_t> localTimeNsForSample(std::uint64_t sampleIndex) const;
    /*!
     * \brief Converts a sample index to global UTC time.
     *
     * \param[in] sampleIndex Original BCO sample index.
     * \return UTC time in nanoseconds or validation issues.
     */
    DomainResult<std::int64_t> globalTimeUtcNsForSample(std::uint64_t sampleIndex) const;
};

/*!
 * \brief Domain-level bearing result prepared outside QML.
 */
struct BearingResult
{
    //! Representative sample index for the result.
    std::uint64_t sampleIndex = 0;
    //! Result time in UTC nanoseconds.
    std::int64_t resultTimeUtcNs = 0;
    //! Related band index.
    int bandIndex = 0;
    //! Calculated bearing azimuth in degrees.
    double bearingAzimuthDeg = 0.0;
    //! Frequencies associated with the result, in hertz.
    std::vector<std::int64_t> frequenciesHz;
    //! Optional normalized quality value in range 0..1.
    std::optional<double> quality;
    //! Domain diagnostics that should be preserved with the result.
    std::vector<ValidationIssue> diagnostics;

    /*!
     * \brief Creates a validated bearing result.
     *
     * \param[in] sampleIndex Representative sample index.
     * \param[in] resultTimeUtcNs Result UTC time in nanoseconds.
     * \param[in] bandIndex Related band index.
     * \param[in] bearingAzimuthDeg Bearing azimuth in degrees.
     * \param[in] frequenciesHz Non-empty related frequency set.
     * \param[in] quality Optional normalized quality value.
     * \param[in] diagnostics Domain diagnostics to preserve with the result.
     * \param[in] capabilities Runtime band and beam limits.
     * \return Created result or validation issues.
     */
    static DomainResult<BearingResult> create(std::uint64_t sampleIndex,
                                              std::int64_t resultTimeUtcNs,
                                              int bandIndex,
                                              double bearingAzimuthDeg,
                                              std::vector<std::int64_t> frequenciesHz,
                                              std::optional<double> quality = std::nullopt,
                                              std::vector<ValidationIssue> diagnostics = {},
                                              const RuntimeCapabilities& capabilities =
                                                  defaultRuntimeCapabilities());

    /*!
     * \brief Validates result time, band, azimuth, frequencies, and quality.
     *
     * \param[in] capabilities Runtime band and beam limits.
     * \return Validation result with all detected issues.
     */
    ValidationResult validate(const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Read-only result table row derived from a bearing result.
 */
struct ResultTableRow
{
    //! Representative sample index for the row.
    std::uint64_t sampleIndex = 0;
    //! Result time in UTC nanoseconds.
    std::int64_t resultTimeUtcNs = 0;
    //! Antenna azimuth at result time, in degrees.
    double antennaAzimuthDeg = 0.0;
    //! Related band index.
    int bandIndex = 0;
    //! Frequencies associated with the result, in hertz.
    std::vector<std::int64_t> frequenciesHz;
    //! Optional normalized quality value in range 0..1.
    std::optional<double> quality;
    //! Domain diagnostics preserved for UI and storage.
    std::vector<ValidationIssue> diagnostics;

    /*!
     * \brief Builds a result table row from a validated bearing result.
     *
     * \param[in] result Bearing result to copy into the row.
     * \param[in] antennaAzimuthDeg Antenna azimuth at result time.
     * \param[in] capabilities Runtime band and beam limits.
     * \return Created row or validation issues.
     */
    static DomainResult<ResultTableRow> fromBearingResult(
        const BearingResult& result,
        double antennaAzimuthDeg,
        const RuntimeCapabilities& capabilities = defaultRuntimeCapabilities());

    /*!
     * \brief Validates row time, antenna azimuth, band, frequencies, and quality.
     *
     * \param[in] capabilities Runtime band and beam limits.
     * \return Validation result with all detected issues.
     */
    ValidationResult validate(const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Validates input amplitude against the current 1..127 range.
 *
 * \param[in] amplitude Input amplitude value.
 * \return Validation result.
 */
ValidationResult validateAmplitude(int amplitude);
/*!
 * \brief Validates a beam index against runtime capabilities.
 *
 * \param[in] beamIndex Beam index to validate.
 * \param[in] capabilities Runtime band and beam limits.
 * \return Validation result.
 */
ValidationResult validateBeamIndex(int beamIndex,
                                   const RuntimeCapabilities& capabilities =
                                       defaultRuntimeCapabilities());
/*!
 * \brief Validates a band index against runtime capabilities.
 *
 * \param[in] bandIndex Band index to validate.
 * \param[in] capabilities Runtime band and beam limits.
 * \return Validation result.
 */
ValidationResult validateBandIndex(int bandIndex,
                                   const RuntimeCapabilities& capabilities =
                                       defaultRuntimeCapabilities());
/*!
 * \brief Validates azimuth in the [0, 360) degree range.
 *
 * \param[in] azimuthDeg Azimuth in degrees.
 * \return Validation result.
 */
ValidationResult validateAzimuth(double azimuthDeg);
/*!
 * \brief Validates absolute frequency against the full system range.
 *
 * \param[in] frequencyHz Absolute frequency in hertz.
 * \return Validation result.
 */
ValidationResult validateSystemFrequency(std::int64_t frequencyHz);

} // namespace siriusscope::core
