#pragma once

/*!
 * \file domain_validation.h
 * \brief Validation result types shared by SiriusScope domain models.
 */

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace siriusscope::core {

/*!
 * \brief Stable validation issue identifiers used by domain and processing diagnostics.
 */
enum class ValidationCode
{
    Ok, //!< No validation problem.
    InvalidAmplitude, //!< Input amplitude is outside the accepted 1..127 range.
    InvalidBeamIndex, //!< Beam index is not valid for current capabilities.
    InvalidBandIndex, //!< Band index is not valid for current capabilities.
    InvalidFrequency, //!< Absolute frequency is outside the system frequency range.
    BandOutOfRange, //!< Band edges leave the system frequency range.
    InvalidBandWidth, //!< Band width is zero, negative, or above the current limit.
    InvalidFrequencyOffset, //!< Sample frequency offset is outside the configured band.
    InvalidAzimuth, //!< Azimuth is outside the accepted [0, 360) degree range.
    InvalidScanSector, //!< Scan sector is invalid or has zero span.
    InvalidSampleIndex, //!< Sample index cannot be mapped by the requested model.
    InvalidTimeBase, //!< Time base parameters are invalid or overflow conversion.
    InvalidQuality, //!< Normalized quality value is outside 0..1.
    EmptyFrequencySet, //!< Result frequency list is empty.
};

/*!
 * \brief One validation issue with a stable code and optional human-readable detail.
 */
struct ValidationIssue
{
    //! Machine-readable issue identifier.
    ValidationCode code = ValidationCode::Ok;
    //! Optional diagnostic text intended for logs or developer-facing status.
    std::string message;
};

/*!
 * \brief Accumulates zero or more validation issues.
 *
 * A default-constructed result is valid. Functions that check several rules
 * should merge all detected issues instead of failing fast when that helps
 * diagnostics.
 */
class ValidationResult
{
public:
    /*!
     * \brief Creates a successful validation result.
     *
     * \return Result without issues.
     */
    static ValidationResult ok()
    {
        return ValidationResult{};
    }

    /*!
     * \brief Creates a failed validation result with one issue.
     *
     * \param[in] code Stable issue code. ValidationCode::Ok is ignored.
     * \param[in] message Optional diagnostic text.
     * \return Result containing the requested issue when the code is not Ok.
     */
    static ValidationResult invalid(ValidationCode code, std::string message = {})
    {
        ValidationResult result;
        result.add(code, std::move(message));
        return result;
    }

    /*!
     * \brief Checks whether no validation issues were collected.
     *
     * \return true when the result is valid.
     */
    bool isValid() const noexcept
    {
        return m_issues.empty();
    }

    /*!
     * \brief Boolean shortcut for isValid().
     */
    explicit operator bool() const noexcept
    {
        return isValid();
    }

    /*!
     * \brief Returns the collected validation issues.
     *
     * \return Ordered issue list. The reference stays valid while this object lives.
     */
    const std::vector<ValidationIssue>& issues() const noexcept
    {
        return m_issues;
    }

    /*!
     * \brief Adds one validation issue.
     *
     * \param[in] code Stable issue code. ValidationCode::Ok is ignored.
     * \param[in] message Optional diagnostic text.
     */
    void add(ValidationCode code, std::string message = {})
    {
        if (code == ValidationCode::Ok) {
            return;
        }

        m_issues.push_back(ValidationIssue{code, std::move(message)});
    }

    /*!
     * \brief Appends issues from another result.
     *
     * \param[in] other Result whose issues are appended in order.
     */
    void merge(const ValidationResult& other)
    {
        m_issues.insert(m_issues.end(), other.m_issues.begin(), other.m_issues.end());
    }

    /*!
     * \brief Checks whether at least one issue has the requested code.
     *
     * \param[in] code Issue code to find.
     * \return true when the code is present.
     */
    bool contains(ValidationCode code) const noexcept
    {
        for (const auto& issue : m_issues) {
            if (issue.code == code) {
                return true;
            }
        }

        return false;
    }

private:
    std::vector<ValidationIssue> m_issues;
};

/*!
 * \brief Domain operation result with an optional value and validation details.
 *
 * A result can carry validation issues even when a value is present. The
 * boolean conversion is deliberately stricter than hasValue(): it returns true
 * only when the value exists and validation has no issues.
 *
 * \tparam T Stored domain value type.
 */
template <typename T>
class DomainResult
{
public:
    /*!
     * \brief Creates a result with a value.
     *
     * \param[in] value Domain value to store.
     * \param[in] validation Validation details associated with the value.
     * \return Successful value wrapper.
     */
    static DomainResult success(T value, ValidationResult validation = ValidationResult::ok())
    {
        return DomainResult(std::move(value), std::move(validation));
    }

    /*!
     * \brief Creates a result without a value.
     *
     * \param[in] validation Validation details explaining why no value exists.
     * \return Failed value wrapper.
     */
    static DomainResult failure(ValidationResult validation)
    {
        return DomainResult(std::nullopt, std::move(validation));
    }

    /*!
     * \brief Checks whether the wrapper stores a value.
     *
     * \return true when value() can return a non-null pointer.
     */
    bool hasValue() const noexcept
    {
        return m_value.has_value();
    }

    /*!
     * \brief Checks whether the result contains a valid value.
     *
     * \return true only when a value exists and validation is valid.
     */
    explicit operator bool() const noexcept
    {
        return hasValue() && m_validation.isValid();
    }

    /*!
     * \brief Returns a pointer to the stored value.
     *
     * \return Stored value pointer, or nullptr when the result has no value.
     */
    const T* value() const noexcept
    {
        return m_value ? &(*m_value) : nullptr;
    }

    /*!
     * \brief Returns a mutable pointer to the stored value.
     *
     * \return Stored value pointer, or nullptr when the result has no value.
     */
    T* value() noexcept
    {
        return m_value ? &(*m_value) : nullptr;
    }

    /*!
     * \brief Returns validation details for success and failure cases.
     *
     * \return Validation result stored in this wrapper.
     */
    const ValidationResult& validation() const noexcept
    {
        return m_validation;
    }

private:
    DomainResult(std::optional<T> value, ValidationResult validation)
        : m_value(std::move(value))
        , m_validation(std::move(validation))
    {
    }

    std::optional<T> m_value;
    ValidationResult m_validation;
};

} // namespace siriusscope::core
