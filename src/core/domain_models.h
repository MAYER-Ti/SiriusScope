#pragma once

/*!
 * \file domain_models.h
 * \brief Доменные модели SiriusScope и точки входа валидации.
 */

#include "core/domain_constraints.h"
#include "core/domain_validation.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::core {

/*!
 * \brief Включительный частотный интервал, Гц.
 */
struct FrequencyRange
{
    //! Нижняя граница частоты, Гц.
    std::int64_t minHz = 0;
    //! Верхняя граница частоты, Гц.
    std::int64_t maxHz = 0;

    /*!
     * \brief Возвращает ширину интервала.
     *
     * \return maxHz - minHz. Некорректные диапазоны могут дать отрицательное значение.
     */
    std::int64_t widthHz() const noexcept;
    /*!
     * \brief Проверяет, принадлежит ли частота этому включительному диапазону.
     *
     * \param[in] frequencyHz Частота, Гц.
     * \return true, если frequencyHz находится внутри [minHz, maxHz].
     */
    bool contains(std::int64_t frequencyHz) const noexcept;
};

/*!
 * \brief Конфигурация одной полосы BCO, представленной BandItem.
 *
 * Модель не зависит от пользовательского интерфейса. Изменения из интерфейса
 * должны проходить валидацию в контроллерах приложения до попадания в адаптеры
 * аппаратных команд.
 */
struct BandConfig
{
    //! Индекс полосы, начиная с нуля.
    int bandIndex = 0;
    //! Центральная частота полосы, Гц.
    std::int64_t centerFrequencyHz = DomainConstraints::minSystemFrequencyHz;
    //! Ширина полосы, Гц. Текущий максимум — 500 МГц.
    std::int64_t widthHz = DomainConstraints::maxBandWidthHz;
    //! Признак участия полосы в приеме и визуализации.
    bool enabled = true;

    /*!
     * \brief Создает провалидированную конфигурацию полосы.
     *
     * \param[in] bandIndex Индекс полосы, начиная с нуля.
     * \param[in] centerFrequencyHz Центральная частота полосы, Гц.
     * \param[in] widthHz Ширина полосы, Гц.
     * \param[in] enabled Признак включения полосы.
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Созданная конфигурация или ошибки валидации.
     */
    static DomainResult<BandConfig> create(int bandIndex,
                                           std::int64_t centerFrequencyHz,
                                           std::int64_t widthHz,
                                           bool enabled = true,
                                           const RuntimeCapabilities& capabilities =
                                               defaultRuntimeCapabilities());

    /*!
     * \brief Возвращает включительный частотный диапазон полосы.
     *
     * \return Центральная частота плюс/минус половина заданной ширины.
     */
    FrequencyRange frequencyRange() const noexcept;
    /*!
     * \brief Проверяет, находится ли абсолютная частота внутри этой полосы.
     *
     * \param[in] frequencyHz Абсолютная частота, Гц.
     * \return true, если частота принадлежит frequencyRange().
     */
    bool containsFrequency(std::int64_t frequencyHz) const noexcept;
    /*!
     * \brief Валидирует индекс, центральную частоту, ширину и системные границы.
     *
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Результат валидации со всеми найденными ошибками.
     */
    ValidationResult validate(const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Сырой отсчет в координатах луча до привязки к полосе.
 */
struct BeamSample
{
    //! Исходный индекс отсчета BCO. Должен сохраняться всеми вышестоящими слоями.
    std::uint64_t sampleIndex = 0;
    //! Смещение частоты от центра полосы, Гц.
    std::int64_t frequencyOffsetHz = 0;
    //! Входная амплитуда. Допустимый диапазон 1..127.
    int amplitude = 0;
    //! Индекс луча из текущей модели антенны.
    int beamIndex = 0;
};

/*!
 * \brief Провалидированный сигнальный отсчет с абсолютной частотой и полосой.
 */
struct SignalSample
{
    //! Исходный индекс отсчета BCO.
    std::uint64_t sampleIndex = 0;
    //! Индекс полосы, начиная с нуля.
    int bandIndex = 0;
    //! Смещение частоты от центра полосы, Гц.
    std::int64_t frequencyOffsetHz = 0;
    //! Абсолютная частота отсчета, Гц.
    std::int64_t absoluteFrequencyHz = 0;
    //! Входная амплитуда в допустимом диапазоне 1..127.
    int amplitude = 0;
    //! Индекс луча в текущих возможностях времени выполнения.
    int beamIndex = 0;

    /*!
     * \brief Создает провалидированный сигнальный отсчет из отсчета луча и полосы.
     *
     * \param[in] beamSample Отсчет в координатах луча от парсера или симулятора.
     * \param[in] bandConfig Полоса для вычисления абсолютной частоты.
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Созданный отсчет или ошибки валидации.
     */
    static DomainResult<SignalSample> create(const BeamSample& beamSample,
                                             const BandConfig& bandConfig,
                                             const RuntimeCapabilities& capabilities =
                                                 defaultRuntimeCapabilities());

    /*!
     * \brief Валидирует поля отсчета относительно связанной конфигурации полосы.
     *
     * \param[in] bandConfig Полоса, которой должен принадлежать отсчет.
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Результат валидации со всеми найденными ошибками.
     */
    ValidationResult validate(const BandConfig& bandConfig,
                              const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Сектор сканирования антенны в градусах.
 *
 * Диапазон использует азимуты [0, 360). Сектор проходит через север,
 * если startAzimuthDeg больше endAzimuthDeg.
 */
struct ScanSector
{
    //! Начальный азимут сектора, градусы, включительно.
    double startAzimuthDeg = 0.0;
    //! Конечный азимут сектора, градусы, включительно для contains().
    double endAzimuthDeg = 0.0;

    /*!
     * \brief Создает провалидированный сектор сканирования.
     *
     * \param[in] startAzimuthDeg Начало сектора, градусы.
     * \param[in] endAzimuthDeg Конец сектора, градусы.
     * \return Созданный сектор или ошибки валидации.
     */
    static DomainResult<ScanSector> create(double startAzimuthDeg, double endAzimuthDeg);

    /*!
     * \brief Валидирует границы азимута и ненулевую ширину сектора.
     *
     * \return Результат валидации.
     */
    ValidationResult validate() const;
    /*!
     * \brief Проверяет, пересекает ли сектор границу 360/0 градусов.
     *
     * \return true, если startAzimuthDeg > endAzimuthDeg.
     */
    bool isWrapAround() const noexcept;
    /*!
     * \brief Проверяет, принадлежит ли азимут этому сектору.
     *
     * \param[in] azimuthDeg Азимут, градусы.
     * \return true для азимутов внутри сектора.
     */
    bool contains(double azimuthDeg) const noexcept;
    /*!
     * \brief Вычисляет ширину сектора в градусах.
     *
     * \return Ширина сектора с учетом перехода через 360/0 градусов.
     */
    double spanDegrees() const noexcept;
};

/*!
 * \brief Преобразует сохраненные индексы отсчетов BCO в локальное и UTC-время.
 */
struct TimeBase
{
    //! UTC-время начала записи, наносекунды.
    std::int64_t recordingStartUtcNs = 0;
    //! Первый индекс отсчета BCO в записи.
    std::uint64_t firstSampleIndex = 0;
    //! Длительность одного шага отсчета, наносекунды.
    std::uint64_t samplePeriodNs = DomainConstraints::defaultSamplePeriodNs;

    /*!
     * \brief Создает провалидированную временную базу.
     *
     * \param[in] recordingStartUtcNs UTC-время начала записи, наносекунды.
     * \param[in] firstSampleIndex Первый индекс отсчета BCO в записи.
     * \param[in] samplePeriodNs Длительность одного шага отсчета, наносекунды.
     * \return Созданная временная база или ошибки валидации.
     */
    static DomainResult<TimeBase> create(std::int64_t recordingStartUtcNs,
                                         std::uint64_t firstSampleIndex,
                                         std::uint64_t samplePeriodNs);

    /*!
     * \brief Валидирует начало отсчета времени и период отсчетов.
     *
     * \return Результат валидации.
     */
    ValidationResult validate() const;
    /*!
     * \brief Преобразует индекс отсчета во время от начала записи.
     *
     * \param[in] sampleIndex Исходный индекс отсчета BCO.
     * \return Локальное время в наносекундах или ошибки валидации.
     */
    DomainResult<std::int64_t> localTimeNsForSample(std::uint64_t sampleIndex) const;
    /*!
     * \brief Преобразует индекс отсчета в глобальное UTC-время.
     *
     * \param[in] sampleIndex Исходный индекс отсчета BCO.
     * \return UTC-время в наносекундах или ошибки валидации.
     */
    DomainResult<std::int64_t> globalTimeUtcNsForSample(std::uint64_t sampleIndex) const;
};

/*!
 * \brief Доменный результат пеленгации, подготовленный вне QML.
 */
struct BearingResult
{
    //! Представительный индекс отсчета для результата.
    std::uint64_t sampleIndex = 0;
    //! Время результата в UTC, наносекунды.
    std::int64_t resultTimeUtcNs = 0;
    //! Связанный индекс полосы.
    int bandIndex = 0;
    //! Рассчитанный азимут пеленга, градусы.
    double bearingAzimuthDeg = 0.0;
    //! Частоты, связанные с результатом, Гц.
    std::vector<std::int64_t> frequenciesHz;
    //! Необязательное нормализованное качество в диапазоне 0..1.
    std::optional<double> quality;
    //! Доменные диагностики, которые должны сохраняться вместе с результатом.
    std::vector<ValidationIssue> diagnostics;

    /*!
     * \brief Создает провалидированный результат пеленгации.
     *
     * \param[in] sampleIndex Представительный индекс отсчета.
     * \param[in] resultTimeUtcNs UTC-время результата, наносекунды.
     * \param[in] bandIndex Связанный индекс полосы.
     * \param[in] bearingAzimuthDeg Азимут пеленга, градусы.
     * \param[in] frequenciesHz Непустой набор связанных частот.
     * \param[in] quality Необязательное нормализованное качество.
     * \param[in] diagnostics Доменные диагностики для сохранения с результатом.
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Созданный результат или ошибки валидации.
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
     * \brief Валидирует время результата, полосу, азимут, частоты и качество.
     *
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Результат валидации со всеми найденными ошибками.
     */
    ValidationResult validate(const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Строка таблицы результатов только для чтения, полученная из пеленга.
 */
struct ResultTableRow
{
    //! Представительный индекс отсчета для строки.
    std::uint64_t sampleIndex = 0;
    //! Время результата в UTC, наносекунды.
    std::int64_t resultTimeUtcNs = 0;
    //! Рассчитанный азимут пеленга, градусы.
    double bearingAzimuthDeg = 0.0;
    //! Азимут антенны в момент результата, градусы.
    double antennaAzimuthDeg = 0.0;
    //! Связанный индекс полосы.
    int bandIndex = 0;
    //! Частоты, связанные с результатом, Гц.
    std::vector<std::int64_t> frequenciesHz;
    //! Необязательное нормализованное качество в диапазоне 0..1.
    std::optional<double> quality;
    //! Доменные диагностики, сохраняемые для пользовательского интерфейса и хранилища.
    std::vector<ValidationIssue> diagnostics;

    /*!
     * \brief Строит строку таблицы результатов из провалидированного пеленга.
     *
     * \param[in] result Результат пеленгации для копирования в строку.
     * \param[in] antennaAzimuthDeg Азимут антенны в момент результата.
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Созданная строка или ошибки валидации.
     */
    static DomainResult<ResultTableRow> fromBearingResult(
        const BearingResult& result,
        double antennaAzimuthDeg,
        const RuntimeCapabilities& capabilities = defaultRuntimeCapabilities());

    /*!
     * \brief Валидирует время строки, азимут антенны, полосу, частоты и качество.
     *
     * \param[in] capabilities Ограничения полос и лучей времени выполнения.
     * \return Результат валидации со всеми найденными ошибками.
     */
    ValidationResult validate(const RuntimeCapabilities& capabilities =
                                  defaultRuntimeCapabilities()) const;
};

/*!
 * \brief Валидирует входную амплитуду по текущему диапазону 1..127.
 *
 * \param[in] amplitude Значение входной амплитуды.
 * \return Результат валидации.
 */
ValidationResult validateAmplitude(int amplitude);
/*!
 * \brief Валидирует индекс луча по возможностям времени выполнения.
 *
 * \param[in] beamIndex Проверяемый индекс луча.
 * \param[in] capabilities Ограничения полос и лучей времени выполнения.
 * \return Результат валидации.
 */
ValidationResult validateBeamIndex(int beamIndex,
                                   const RuntimeCapabilities& capabilities =
                                       defaultRuntimeCapabilities());
/*!
 * \brief Валидирует индекс полосы по возможностям времени выполнения.
 *
 * \param[in] bandIndex Проверяемый индекс полосы.
 * \param[in] capabilities Ограничения полос и лучей времени выполнения.
 * \return Результат валидации.
 */
ValidationResult validateBandIndex(int bandIndex,
                                   const RuntimeCapabilities& capabilities =
                                       defaultRuntimeCapabilities());
/*!
 * \brief Валидирует азимут в диапазоне [0, 360) градусов.
 *
 * \param[in] azimuthDeg Азимут, градусы.
 * \return Результат валидации.
 */
ValidationResult validateAzimuth(double azimuthDeg);
/*!
 * \brief Валидирует абсолютную частоту по полному системному диапазону.
 *
 * \param[in] frequencyHz Абсолютная частота, Гц.
 * \return Результат валидации.
 */
ValidationResult validateSystemFrequency(std::int64_t frequencyHz);

} // namespace siriusscope::core
