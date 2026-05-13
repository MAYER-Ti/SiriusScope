#pragma once

/*!
 * \file domain_validation.h
 * \brief Типы результатов валидации, общие для доменных моделей SiriusScope.
 */

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace siriusscope::core {

/*!
 * \brief Стабильные идентификаторы ошибок валидации для домена и обработки.
 */
enum class ValidationCode
{
    Ok, //!< Ошибка валидации отсутствует.
    InvalidAmplitude, //!< Входная амплитуда вне допустимого диапазона 1..127.
    InvalidBeamIndex, //!< Индекс луча недопустим для текущих возможностей.
    InvalidBandIndex, //!< Индекс полосы недопустим для текущих возможностей.
    InvalidFrequency, //!< Абсолютная частота вне системного частотного диапазона.
    BandOutOfRange, //!< Границы полосы выходят за системный частотный диапазон.
    InvalidBandWidth, //!< Ширина полосы нулевая, отрицательная или выше предела.
    InvalidFrequencyOffset, //!< Смещение частоты отсчета вне настроенной полосы.
    InvalidAzimuth, //!< Азимут вне допустимого диапазона [0, 360) градусов.
    InvalidScanSector, //!< Сектор сканирования недопустим или имеет нулевую ширину.
    InvalidSampleIndex, //!< Индекс отсчета нельзя отобразить выбранной моделью.
    InvalidTimeBase, //!< Параметры временной базы недопустимы или дают переполнение.
    InvalidQuality, //!< Нормализованное значение качества вне диапазона 0..1.
    EmptyFrequencySet, //!< Список частот результата пуст.
};

/*!
 * \brief Одна ошибка валидации со стабильным кодом и необязательным описанием.
 */
struct ValidationIssue
{
    //! Машиночитаемый идентификатор ошибки.
    ValidationCode code = ValidationCode::Ok;
    //! Необязательный диагностический текст для логов или статуса разработчика.
    std::string message;
};

/*!
 * \brief Накапливает ноль или более ошибок валидации.
 *
 * Результат, созданный по умолчанию, считается успешным. Функции, проверяющие
 * несколько правил, должны объединять все найденные ошибки, если это улучшает
 * диагностику.
 */
class ValidationResult
{
public:
    /*!
     * \brief Создает успешный результат валидации.
     *
     * \return Результат без ошибок.
     */
    static ValidationResult ok()
    {
        return ValidationResult{};
    }

    /*!
     * \brief Создает неуспешный результат валидации с одной ошибкой.
     *
     * \param[in] code Стабильный код ошибки. ValidationCode::Ok игнорируется.
     * \param[in] message Необязательный диагностический текст.
     * \return Результат с указанной ошибкой, если код не равен Ok.
     */
    static ValidationResult invalid(ValidationCode code, std::string message = {})
    {
        ValidationResult result;
        result.add(code, std::move(message));
        return result;
    }

    /*!
     * \brief Проверяет, что ошибки валидации не были собраны.
     *
     * \return true, если результат успешен.
     */
    bool isValid() const noexcept
    {
        return m_issues.empty();
    }

    /*!
     * \brief Булево сокращение для isValid().
     */
    explicit operator bool() const noexcept
    {
        return isValid();
    }

    /*!
     * \brief Возвращает собранные ошибки валидации.
     *
     * \return Упорядоченный список ошибок. Ссылка действительна, пока жив объект.
     */
    const std::vector<ValidationIssue>& issues() const noexcept
    {
        return m_issues;
    }

    /*!
     * \brief Добавляет одну ошибку валидации.
     *
     * \param[in] code Стабильный код ошибки. ValidationCode::Ok игнорируется.
     * \param[in] message Необязательный диагностический текст.
     */
    void add(ValidationCode code, std::string message = {})
    {
        if (code == ValidationCode::Ok) {
            return;
        }

        m_issues.push_back(ValidationIssue{code, std::move(message)});
    }

    /*!
     * \brief Добавляет ошибки из другого результата.
     *
     * \param[in] other Результат, ошибки которого добавляются с сохранением порядка.
     */
    void merge(const ValidationResult& other)
    {
        m_issues.insert(m_issues.end(), other.m_issues.begin(), other.m_issues.end());
    }

    /*!
     * \brief Проверяет, есть ли хотя бы одна ошибка с заданным кодом.
     *
     * \param[in] code Код ошибки для поиска.
     * \return true, если код найден.
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
 * \brief Результат доменной операции с необязательным значением и валидацией.
 *
 * Результат может содержать ошибки валидации даже при наличии значения.
 * Булево преобразование намеренно строже, чем hasValue(): оно возвращает true
 * только при наличии значения и отсутствии ошибок валидации.
 *
 * \tparam T Тип сохраняемого доменного значения.
 */
template <typename T>
class DomainResult
{
public:
    /*!
     * \brief Создает результат со значением.
     *
     * \param[in] value Доменное значение для хранения.
     * \param[in] validation Валидация, связанная со значением.
     * \return Обертка успешного значения.
     */
    static DomainResult success(T value, ValidationResult validation = ValidationResult::ok())
    {
        return DomainResult(std::move(value), std::move(validation));
    }

    /*!
     * \brief Создает результат без значения.
     *
     * \param[in] validation Валидация, объясняющая отсутствие значения.
     * \return Обертка неуспешного результата.
     */
    static DomainResult failure(ValidationResult validation)
    {
        return DomainResult(std::nullopt, std::move(validation));
    }

    /*!
     * \brief Проверяет, хранит ли обертка значение.
     *
     * \return true, если value() может вернуть ненулевой указатель.
     */
    bool hasValue() const noexcept
    {
        return m_value.has_value();
    }

    /*!
     * \brief Проверяет, содержит ли результат корректное значение.
     *
     * \return true только при наличии значения и успешной валидации.
     */
    explicit operator bool() const noexcept
    {
        return hasValue() && m_validation.isValid();
    }

    /*!
     * \brief Возвращает указатель на сохраненное значение.
     *
     * \return Указатель на значение или nullptr, если значения нет.
     */
    const T* value() const noexcept
    {
        return m_value ? &(*m_value) : nullptr;
    }

    /*!
     * \brief Возвращает изменяемый указатель на сохраненное значение.
     *
     * \return Указатель на значение или nullptr, если значения нет.
     */
    T* value() noexcept
    {
        return m_value ? &(*m_value) : nullptr;
    }

    /*!
     * \brief Возвращает сведения валидации для успешных и неуспешных случаев.
     *
     * \return Результат валидации, сохраненный в обертке.
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
