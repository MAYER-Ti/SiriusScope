#pragma once

/*!
 * \file sample_processor.h
 * \brief Независимые от пользовательского интерфейса валидация, агрегация отсчетов и подготовка кадров.
 */

#include "core/domain_models.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace siriusscope::processing {

/*!
 * \brief Стабильные идентификаторы диагностик обработки.
 */
enum class ProcessingErrorCode
{
    None, //!< Проблема обработки отсутствует.
    InvalidSampleRejected, //!< Входной отсчет отклонен доменной валидацией.
    MissingBeamSample, //!< В кандидате пеленгации отсутствует обязательный луч.
    DuplicateSample, //!< Пакет содержит один и тот же идентификатор отсчета повторно.
    OutOfOrderSampleIndex, //!< Индекс отсчета ниже уже принятого индекса.
    AggregationWindowOverflow, //!< Размер пакета превысил настроенное окно обработки.
    FrequencyBinOutOfRange, //!< Отсчет или кадр нельзя отнести к частотному бину.
    InsufficientBearingData, //!< Нет полного кандидата для пеленгации.
    EmptyBatch, //!< Обработчик получил пустой набор отсчетов.
    MissingWaterfallData, //!< В строке или ячейке Waterfall нет данных отсчетов.
};

/*!
 * \brief Уровень важности диагностики обработки для статуса и логов.
 */
enum class ProcessingDiagnosticSeverity
{
    Info, //!< Информационная диагностика.
    Warning, //!< Восстановимая проблема, которая может повлиять на качество результата.
    Error, //!< Отклоненный вход или непригодный результат обработки.
};

/*!
 * \brief Диагностика, выпущенная валидацией, агрегацией или построением кадров.
 */
struct ProcessingDiagnostic
{
    //! Машиночитаемый код диагностики.
    ProcessingErrorCode code = ProcessingErrorCode::None;
    //! Уровень важности для статуса и технических логов.
    ProcessingDiagnosticSeverity severity = ProcessingDiagnosticSeverity::Warning;
    //! Подробный текст для разработчика.
    std::string message;
    //! Необязательный индекс исходного отсчета.
    std::optional<std::uint64_t> sampleIndex;
    //! Необязательный индекс исходной полосы.
    std::optional<int> bandIndex;
    //! Необязательный индекс исходного луча.
    std::optional<int> beamIndex;
    //! Необязательная абсолютная частота, Гц.
    std::optional<std::int64_t> frequencyHz;
    //! Необязательный индекс частотного бина.
    std::optional<std::size_t> frequencyBin;
    //! Ошибки доменной валидации, связанные с диагностикой.
    std::vector<core::ValidationIssue> domainIssues;
};

/*!
 * \brief Настраиваемые ограничения агрегации отсчетов и подготовки пеленгации.
 */
struct AggregationWindow
{
    //! Ширина частотного бина, Гц.
    std::int64_t frequencyBinWidthHz = 1'000'000LL;
    //! Максимум отсчетов в одном пакете до выдачи предупреждения.
    std::size_t maxSamplesPerBatch = 1'000'000;
    //! Индексы лучей, обязательные для полного кандидата пеленгации.
    std::vector<int> requiredBearingBeams = {0, 1};
    //! Признак добавления информационных диагностик в пустые ячейки Waterfall.
    bool diagnoseMissingWaterfallCells = true;
};

/*!
 * \brief Конфигурация, необходимая SampleProcessor.
 */
struct SampleProcessingConfig
{
    //! Конфигурации полос, принимаемые данным экземпляром обработки.
    std::vector<core::BandConfig> bands;
    //! Возможности полос и лучей времени выполнения, используемые для валидации.
    core::RuntimeCapabilities capabilities = core::defaultRuntimeCapabilities();
    //! Ограничения агрегации и построения кадров.
    AggregationWindow aggregationWindow;
};

/*!
 * \brief Пакет уже разобранных сигнальных отсчетов.
 */
struct SampleBatch
{
    //! Отсчеты для валидации, агрегации и преобразования.
    std::vector<core::SignalSample> samples;
};

/*!
 * \brief Принятый отсчет, дополненный данными бина агрегации.
 */
struct ProcessedSample
{
    //! Исходный провалидированный сигнальный отсчет.
    core::SignalSample sample;
    //! Индекс частотного бина внутри полосы отсчета.
    std::size_t frequencyBin = 0;
    //! Включительный частотный диапазон, представленный бином.
    core::FrequencyRange frequencyRange;
};

/*!
 * \brief Агрегированная статистика амплитуды одного луча в одном частотном бине.
 */
struct AggregatedBeamBin
{
    //! Индекс луча, представленный этой агрегацией.
    int beamIndex = 0;
    //! Количество отсчетов, включенных в агрегацию.
    std::size_t sampleCount = 0;
    //! Минимальная амплитуда, встреченная в агрегации.
    int minAmplitude = 0;
    //! Максимальная амплитуда, встреченная в агрегации.
    int maxAmplitude = 0;
    //! Сумма амплитуд для расчета averageAmplitude().
    std::uint64_t amplitudeSum = 0;

    /*!
     * \brief Возвращает среднюю амплитуду для бина этого луча.
     *
     * \return Средняя амплитуда или 0.0, если в бине нет отсчетов.
     */
    double averageAmplitude() const noexcept;
};

/*!
 * \brief Агрегированные данные для одной полосы, индекса отсчета и частотного бина.
 */
struct AggregatedFrequencyBin
{
    //! Индекс полосы, представленный этой агрегацией.
    int bandIndex = 0;
    //! Индекс исходного отсчета, представленный этой агрегацией.
    std::uint64_t sampleIndex = 0;
    //! Индекс частотного бина внутри кадра полосы.
    std::size_t frequencyBin = 0;
    //! Частотный диапазон, покрываемый этим бином.
    core::FrequencyRange frequencyRange;
    //! Агрегации лучей, присутствующие в этом бине.
    std::vector<AggregatedBeamBin> beams;
    //! Диагностики, привязанные к этому бину.
    std::vector<ProcessingDiagnostic> diagnostics;

    /*!
     * \brief Находит агрегированные данные для луча.
     *
     * \param[in] beamIndex Индекс искомого луча.
     * \return Указатель на агрегацию луча или nullptr, если она отсутствует.
     */
    const AggregatedBeamBin* beam(int beamIndex) const noexcept;
};

/*!
 * \brief Агрегированный кадр обработки одной полосы за интервал индексов отсчетов.
 */
struct AggregatedBandFrame
{
    //! Индекс полосы, представленный этим кадром.
    int bandIndex = 0;
    //! Первый индекс отсчета, включенный в кадр.
    std::uint64_t sampleIndexStart = 0;
    //! Последний индекс отсчета, включенный в кадр.
    std::uint64_t sampleIndexEnd = 0;
    //! Полный частотный диапазон полосы, покрываемый кадром.
    core::FrequencyRange frequencyRange;
    //! Агрегированные частотные бины, включенные в кадр.
    std::vector<AggregatedFrequencyBin> bins;
    //! Диагностики, привязанные к этому кадру.
    std::vector<ProcessingDiagnostic> diagnostics;
};

/*!
 * \brief Состояние качества данных ячейки Waterfall.
 */
enum class WaterfallCellStatus
{
    Valid, //!< Ячейка содержит данные принятых отсчетов.
    MissingData, //!< В ячейке нет данных отсчетов для запрошенного бина.
    InvalidData, //!< Данные ячейки были получены, но оказались некорректными.
};

/*!
 * \brief Независимая от пользовательского интерфейса ячейка Waterfall, подготовленная из агрегации.
 */
struct WaterfallCell
{
    //! Индекс частотного бина внутри строки.
    std::size_t frequencyBin = 0;
    //! Частотный диапазон, покрываемый этой ячейкой.
    core::FrequencyRange frequencyRange;
    //! Максимальная амплитуда среди всех присутствующих лучей.
    int maxAmplitude = 0;
    //! Средняя амплитуда по всем отсчетам и присутствующим лучам.
    double averageAmplitude = 0.0;
    //! Амплитуды по лучам, индексированные индексом луча.
    std::vector<int> beamAmplitudes;
    //! Флаги присутствия лучей, индексированные индексом луча.
    std::vector<bool> beamPresent;
    //! Состояние данных ячейки.
    WaterfallCellStatus status = WaterfallCellStatus::MissingData;
    //! Диагностики, привязанные к этой ячейке.
    std::vector<ProcessingDiagnostic> diagnostics;
};

/*!
 * \brief Одна подготовленная строка Waterfall для полосы и интервала отсчетов.
 */
struct WaterfallRow
{
    //! Индекс полосы, представленный этой строкой.
    int bandIndex = 0;
    //! Первый индекс отсчета, представленный этой строкой.
    std::uint64_t sampleIndexStart = 0;
    //! Последний индекс отсчета, представленный этой строкой.
    std::uint64_t sampleIndexEnd = 0;
    //! Полный частотный диапазон, представленный этой строкой.
    core::FrequencyRange frequencyRange;
    //! Упорядоченные ячейки Waterfall по частотным бинам.
    std::vector<WaterfallCell> cells;
    //! Диагностики, привязанные к этой строке.
    std::vector<ProcessingDiagnostic> diagnostics;

    /*!
     * \brief Проверяет, есть ли хотя бы одна ячейка без корректных данных.
     *
     * \return true, если ячейка находится в состоянии MissingData или InvalidData.
     */
    bool hasMissingData() const noexcept;
};

/*!
 * \brief Набор подготовленных строк Waterfall и общих диагностик.
 */
struct WaterfallFrame
{
    //! Строки, подготовленные для передачи в UI или хранилище.
    std::vector<WaterfallRow> rows;
    //! Диагностики, относящиеся к кадру Waterfall в целом.
    std::vector<ProcessingDiagnostic> diagnostics;
};

/*!
 * \brief Кандидат входных данных для будущего алгоритма пеленгации.
 */
struct BearingCandidate
{
    //! Индекс полосы, представленный этим кандидатом.
    int bandIndex = 0;
    //! Первый индекс исходного отсчета.
    std::uint64_t sampleIndexStart = 0;
    //! Последний индекс исходного отсчета.
    std::uint64_t sampleIndexEnd = 0;
    //! Индекс частотного бина внутри полосы.
    std::size_t frequencyBin = 0;
    //! Частотный диапазон, покрываемый этим кандидатом.
    core::FrequencyRange frequencyRange;
    //! Амплитуды по лучам, индексированные индексом луча.
    std::vector<int> beamAmplitudes;
    //! Флаги присутствия лучей, индексированные индексом луча.
    std::vector<bool> beamPresent;

    /*!
     * \brief Проверяет, есть ли у кандидата данные для заданного луча.
     *
     * \param[in] beamIndex Проверяемый индекс луча.
     * \return true, если луч находится в диапазоне и присутствует.
     */
    bool hasBeam(int beamIndex) const noexcept;
};

/*!
 * \brief Промежуточный входной кадр пеленгации, подготовленный вне алгоритма.
 */
struct BearingInputFrame
{
    //! Индекс полосы, представленный этим кадром.
    int bandIndex = 0;
    //! Первый индекс отсчета, представленный этим кадром.
    std::uint64_t sampleIndexStart = 0;
    //! Последний индекс отсчета, представленный этим кадром.
    std::uint64_t sampleIndexEnd = 0;
    //! Кандидаты, у которых присутствуют все обязательные лучи.
    std::vector<BearingCandidate> candidates;
    //! Диагностики по отсутствующим лучам или недостаточным данным.
    std::vector<ProcessingDiagnostic> diagnostics;

    /*!
     * \brief Проверяет, можно ли передавать кадр в расчет пеленгации.
     *
     * \return true, если есть хотя бы один кандидат и у кадра нет диагностики
     *         недостаточных данных.
     */
    bool hasSufficientData() const noexcept;
};

/*!
 * \brief Полный результат обработки одного отсчета или одного пакета.
 */
struct SampleProcessingResult
{
    //! Принятые отсчеты с метаданными частотных бинов.
    std::vector<ProcessedSample> acceptedSamples;
    //! Диагностики, собранные при валидации и построении кадров.
    std::vector<ProcessingDiagnostic> diagnostics;
    //! Агрегированные кадры, сгруппированные по полосам.
    std::vector<AggregatedBandFrame> aggregatedBandFrames;
    //! Кадр, готовый для Waterfall.
    WaterfallFrame waterfallFrame;
    //! Входные кадры пеленгации, сгруппированные по полосам.
    std::vector<BearingInputFrame> bearingFrames;

    /*!
     * \brief Проверяет, прошел ли валидацию хотя бы один отсчет.
     *
     * \return true, если acceptedSamples не пуст.
     */
    bool hasAcceptedSamples() const noexcept;
    /*!
     * \brief Ищет код во всех вложенных диагностиках.
     *
     * \param[in] code Код диагностики для поиска.
     * \return true, если код найден в любой части результата.
     */
    bool hasDiagnostic(ProcessingErrorCode code) const noexcept;
};

/*!
 * \brief Строит независимые от пользовательского интерфейса строки Waterfall из агрегированных кадров полос.
 */
class WaterfallRowBuilder
{
public:
    /*!
     * \brief Преобразует агрегированные кадры полос в строки Waterfall.
     *
     * \param[in] frames Агрегированные входные кадры.
     * \param[in] config Конфигурация обработки и ограничения агрегации.
     * \return Подготовленный кадр Waterfall.
     */
    WaterfallFrame build(const std::vector<AggregatedBandFrame>& frames,
                         const SampleProcessingConfig& config) const;
};

/*!
 * \brief Строит промежуточные входные кадры пеленгации из агрегированных кадров.
 */
class BearingFrameBuilder
{
public:
    /*!
     * \brief Извлекает полные кандидаты лучей по бинам для расчета пеленгации.
     *
     * \param[in] frames Агрегированные входные кадры.
     * \param[in] config Конфигурация обработки и набор обязательных лучей.
     * \return Входные кадры пеленгации, сгруппированные по полосам.
     */
    std::vector<BearingInputFrame> build(const std::vector<AggregatedBandFrame>& frames,
                                         const SampleProcessingConfig& config) const;
};

/*!
 * \brief Валидирует отсчеты и готовит данные агрегации, Waterfall и пеленгации.
 *
 * SampleProcessor относится к слою обработки. Он зависит только от моделей
 * core/domain и должен оставаться независимым от QML, аппаратных сокетов
 * и реализаций хранилища.
 */
class SampleProcessor
{
public:
    /*!
     * \brief Создает обработчик с неизменяемой конфигурацией обработки.
     *
     * \param[in] config Конфигурация полос и агрегации.
     */
    explicit SampleProcessor(SampleProcessingConfig config);

    /*!
     * \brief Обрабатывает один сигнальный отсчет.
     *
     * \param[in] sample Разобранный доменный отсчет.
     * \return Результат обработки пакета из одного отсчета.
     */
    SampleProcessingResult processSample(const core::SignalSample& sample);
    /*!
     * \brief Обрабатывает пакет сигнальных отсчетов.
     *
     * Некорректные отсчеты отклоняются диагностически, а корректные продолжают
     * проходить агрегацию и построение кадров.
     *
     * \param[in] batch Отсчеты для обработки.
     * \return Полный результат обработки.
     */
    SampleProcessingResult processBatch(const SampleBatch& batch);

    /*!
     * \brief Сбрасывает отслеживание последовательности для диагностики порядка.
     */
    void resetSequenceTracking() noexcept;

private:
    const core::BandConfig* bandConfigFor(int bandIndex) const noexcept;
    core::ValidationResult validateSample(const core::SignalSample& sample,
                                          const core::BandConfig*& bandConfig) const;
    std::optional<ProcessedSample> prepareSample(const core::SignalSample& sample,
                                                 const core::BandConfig& bandConfig,
                                                 SampleProcessingResult& result) const;
    std::vector<AggregatedBandFrame> aggregate(
        const std::vector<ProcessedSample>& samples) const;

    SampleProcessingConfig m_config;
    std::optional<std::uint64_t> m_highestAcceptedSampleIndex;
};

} // namespace siriusscope::processing
