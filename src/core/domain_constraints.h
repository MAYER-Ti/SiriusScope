#pragma once

/*!
 * \file domain_constraints.h
 * \brief Current SiriusScope domain limits and runtime capability flags.
 */

#include <cstdint>

namespace siriusscope::core {

/*!
 * \brief Runtime limits that can vary between product configurations.
 *
 * The current iteration uses five bands and two active beams, while the
 * domain model keeps the maximum supported beam count explicit so that
 * future 8-beam support is not blocked by fixed UI assumptions.
 */
struct RuntimeCapabilities
{
    //! Number of configured BCO bands available to processing and UI models.
    int bandCount = 5;
    //! Number of beam indices currently accepted in incoming samples.
    int activeBeamCount = 2;
    //! Upper bound supported by the current domain model and adapters.
    int maxSupportedBeamCount = 8;
};

/*!
 * \brief Product and current-iteration limits used by validation logic.
 */
namespace DomainConstraints {

//! Minimum valid input amplitude. Zero is invalid input data.
inline constexpr int minAmplitude = 1;
//! Maximum valid input amplitude accepted from the BCO stream.
inline constexpr int maxAmplitude = 127;

//! Number of BandItem objects in the current SiriusScope iteration.
inline constexpr int currentBandCount = 5;
//! Number of active beams in the current antenna model.
inline constexpr int currentBeamCount = 2;
//! Reserved upper bound for future 8-beam antenna support.
inline constexpr int futureMaxBeamCount = 8;

//! Lower edge of the full product frequency range, in hertz.
inline constexpr std::int64_t minSystemFrequencyHz = 300'000'000LL;
//! Upper edge of the full product frequency range, in hertz.
inline constexpr std::int64_t maxSystemFrequencyHz = 18'000'000'000LL;
//! Maximum width of one BCO band represented by a BandItem, in hertz.
inline constexpr std::int64_t maxBandWidthHz = 500'000'000LL;
//! Maximum signed offset from a band center for a full-width band.
inline constexpr std::int64_t maxFrequencyOffsetHz = maxBandWidthHz / 2;

//! Default sample period used when protocol metadata has no better value.
inline constexpr std::uint64_t defaultSamplePeriodNs = 320ULL;

//! Inclusive lower azimuth bound, in degrees.
inline constexpr double minAzimuthDeg = 0.0;
//! Exclusive upper azimuth bound, in degrees.
inline constexpr double maxAzimuthDeg = 360.0;
//! Lowest accepted normalized quality value.
inline constexpr double minQuality = 0.0;
//! Highest accepted normalized quality value.
inline constexpr double maxQuality = 1.0;

} // namespace DomainConstraints

/*!
 * \brief Returns capabilities matching the current product iteration.
 *
 * \return Five bands, two active beams, and reserved support for up to eight
 *         beams.
 */
RuntimeCapabilities defaultRuntimeCapabilities() noexcept;

} // namespace siriusscope::core
