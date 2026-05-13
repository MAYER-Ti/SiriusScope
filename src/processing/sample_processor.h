#pragma once

/*!
 * \file sample_processor.h
 * \brief UI-independent sample validation, aggregation, and frame preparation.
 */

#include "core/domain_models.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace siriusscope::processing {

/*!
 * \brief Stable processing diagnostic identifiers.
 */
enum class ProcessingErrorCode
{
    None, //!< No processing issue.
    InvalidSampleRejected, //!< Domain validation rejected an incoming sample.
    MissingBeamSample, //!< A bearing candidate lacks a required beam.
    DuplicateSample, //!< A batch contains the same sample identity more than once.
    OutOfOrderSampleIndex, //!< A sample index is lower than an already accepted index.
    AggregationWindowOverflow, //!< Batch size exceeded the configured processing window.
    FrequencyBinOutOfRange, //!< A sample or frame cannot be mapped to a frequency bin.
    InsufficientBearingData, //!< No complete bearing candidate is available.
    EmptyBatch, //!< The processor received no samples.
    MissingWaterfallData, //!< A waterfall row or cell has no sample data.
};

/*!
 * \brief Processing diagnostic severity for status and logs.
 */
enum class ProcessingDiagnosticSeverity
{
    Info, //!< Informational diagnostic.
    Warning, //!< Recoverable issue that may affect result quality.
    Error, //!< Rejected input or unusable processing output.
};

/*!
 * \brief Diagnostic emitted by validation, aggregation, or frame building.
 */
struct ProcessingDiagnostic
{
    //! Machine-readable diagnostic code.
    ProcessingErrorCode code = ProcessingErrorCode::None;
    //! Severity for status and technical logs.
    ProcessingDiagnosticSeverity severity = ProcessingDiagnosticSeverity::Warning;
    //! Developer-facing detail text.
    std::string message;
    //! Optional source sample index.
    std::optional<std::uint64_t> sampleIndex;
    //! Optional source band index.
    std::optional<int> bandIndex;
    //! Optional source beam index.
    std::optional<int> beamIndex;
    //! Optional absolute frequency in hertz.
    std::optional<std::int64_t> frequencyHz;
    //! Optional frequency bin index.
    std::optional<std::size_t> frequencyBin;
    //! Domain validation issues associated with the diagnostic.
    std::vector<core::ValidationIssue> domainIssues;
};

/*!
 * \brief Tunable limits for sample aggregation and bearing input preparation.
 */
struct AggregationWindow
{
    //! Frequency bin width in hertz.
    std::int64_t frequencyBinWidthHz = 1'000'000LL;
    //! Maximum samples expected in one processing batch before a warning is emitted.
    std::size_t maxSamplesPerBatch = 1'000'000;
    //! Beam indices required for a complete bearing candidate.
    std::vector<int> requiredBearingBeams = {0, 1};
    //! Whether empty waterfall cells should carry informational diagnostics.
    bool diagnoseMissingWaterfallCells = true;
};

/*!
 * \brief Configuration required by SampleProcessor.
 */
struct SampleProcessingConfig
{
    //! Band configurations accepted by this processing instance.
    std::vector<core::BandConfig> bands;
    //! Runtime band and beam capabilities used for validation.
    core::RuntimeCapabilities capabilities = core::defaultRuntimeCapabilities();
    //! Aggregation and frame-building limits.
    AggregationWindow aggregationWindow;
};

/*!
 * \brief Batch of already parsed signal samples.
 */
struct SampleBatch
{
    //! Samples to validate, aggregate, and transform.
    std::vector<core::SignalSample> samples;
};

/*!
 * \brief Accepted sample enriched with its aggregation bin.
 */
struct ProcessedSample
{
    //! Original validated signal sample.
    core::SignalSample sample;
    //! Frequency bin index inside the sample band.
    std::size_t frequencyBin = 0;
    //! Inclusive frequency range represented by the bin.
    core::FrequencyRange frequencyRange;
};

/*!
 * \brief Aggregated amplitude statistics for one beam in one frequency bin.
 */
struct AggregatedBeamBin
{
    //! Beam index represented by this aggregate.
    int beamIndex = 0;
    //! Number of samples folded into the aggregate.
    std::size_t sampleCount = 0;
    //! Minimum amplitude observed in the aggregate.
    int minAmplitude = 0;
    //! Maximum amplitude observed in the aggregate.
    int maxAmplitude = 0;
    //! Sum of amplitudes used to calculate averageAmplitude().
    std::uint64_t amplitudeSum = 0;

    /*!
     * \brief Returns the mean amplitude for this beam bin.
     *
     * \return Average amplitude, or 0.0 when the bin has no samples.
     */
    double averageAmplitude() const noexcept;
};

/*!
 * \brief Aggregated data for one band, sample index, and frequency bin.
 */
struct AggregatedFrequencyBin
{
    //! Band index represented by this aggregate.
    int bandIndex = 0;
    //! Source sample index represented by this aggregate.
    std::uint64_t sampleIndex = 0;
    //! Frequency bin index inside the band frame.
    std::size_t frequencyBin = 0;
    //! Frequency range covered by this bin.
    core::FrequencyRange frequencyRange;
    //! Beam aggregates present in this bin.
    std::vector<AggregatedBeamBin> beams;
    //! Diagnostics attached to this bin.
    std::vector<ProcessingDiagnostic> diagnostics;

    /*!
     * \brief Finds aggregate data for a beam.
     *
     * \param[in] beamIndex Beam index to find.
     * \return Pointer to the beam aggregate, or nullptr when absent.
     */
    const AggregatedBeamBin* beam(int beamIndex) const noexcept;
};

/*!
 * \brief Aggregated processing frame for one band over a sample-index interval.
 */
struct AggregatedBandFrame
{
    //! Band index represented by this frame.
    int bandIndex = 0;
    //! First sample index included in this frame.
    std::uint64_t sampleIndexStart = 0;
    //! Last sample index included in this frame.
    std::uint64_t sampleIndexEnd = 0;
    //! Full band frequency range covered by the frame.
    core::FrequencyRange frequencyRange;
    //! Aggregated frequency bins included in the frame.
    std::vector<AggregatedFrequencyBin> bins;
    //! Diagnostics attached to this frame.
    std::vector<ProcessingDiagnostic> diagnostics;
};

/*!
 * \brief Waterfall cell data quality state.
 */
enum class WaterfallCellStatus
{
    Valid, //!< Cell contains accepted sample data.
    MissingData, //!< Cell has no sample data for the requested bin.
    InvalidData, //!< Cell data was present but invalid.
};

/*!
 * \brief UI-independent waterfall cell prepared from aggregated samples.
 */
struct WaterfallCell
{
    //! Frequency bin index inside the row.
    std::size_t frequencyBin = 0;
    //! Frequency range covered by this cell.
    core::FrequencyRange frequencyRange;
    //! Maximum amplitude across all present beams.
    int maxAmplitude = 0;
    //! Average amplitude across all samples and present beams.
    double averageAmplitude = 0.0;
    //! Per-beam amplitude values indexed by beam index.
    std::vector<int> beamAmplitudes;
    //! Per-beam presence flags indexed by beam index.
    std::vector<bool> beamPresent;
    //! Cell data status.
    WaterfallCellStatus status = WaterfallCellStatus::MissingData;
    //! Diagnostics attached to this cell.
    std::vector<ProcessingDiagnostic> diagnostics;
};

/*!
 * \brief One prepared waterfall row for a band and sample-index interval.
 */
struct WaterfallRow
{
    //! Band index represented by this row.
    int bandIndex = 0;
    //! First sample index represented by this row.
    std::uint64_t sampleIndexStart = 0;
    //! Last sample index represented by this row.
    std::uint64_t sampleIndexEnd = 0;
    //! Full frequency range represented by this row.
    core::FrequencyRange frequencyRange;
    //! Ordered waterfall cells by frequency bin.
    std::vector<WaterfallCell> cells;
    //! Diagnostics attached to this row.
    std::vector<ProcessingDiagnostic> diagnostics;

    /*!
     * \brief Checks whether at least one cell lacks valid data.
     *
     * \return true when a cell is MissingData or InvalidData.
     */
    bool hasMissingData() const noexcept;
};

/*!
 * \brief Collection of prepared waterfall rows and global diagnostics.
 */
struct WaterfallFrame
{
    //! Rows prepared for UI or storage handoff.
    std::vector<WaterfallRow> rows;
    //! Diagnostics that apply to the waterfall frame as a whole.
    std::vector<ProcessingDiagnostic> diagnostics;
};

/*!
 * \brief Candidate input for a future bearing algorithm.
 */
struct BearingCandidate
{
    //! Band index represented by this candidate.
    int bandIndex = 0;
    //! First source sample index.
    std::uint64_t sampleIndexStart = 0;
    //! Last source sample index.
    std::uint64_t sampleIndexEnd = 0;
    //! Frequency bin index inside the band.
    std::size_t frequencyBin = 0;
    //! Frequency range covered by this candidate.
    core::FrequencyRange frequencyRange;
    //! Per-beam amplitude values indexed by beam index.
    std::vector<int> beamAmplitudes;
    //! Per-beam presence flags indexed by beam index.
    std::vector<bool> beamPresent;

    /*!
     * \brief Checks whether this candidate has data for the requested beam.
     *
     * \param[in] beamIndex Beam index to check.
     * \return true when the beam is in range and present.
     */
    bool hasBeam(int beamIndex) const noexcept;
};

/*!
 * \brief Intermediate bearing input frame prepared outside the bearing algorithm.
 */
struct BearingInputFrame
{
    //! Band index represented by this frame.
    int bandIndex = 0;
    //! First sample index represented by this frame.
    std::uint64_t sampleIndexStart = 0;
    //! Last sample index represented by this frame.
    std::uint64_t sampleIndexEnd = 0;
    //! Candidates with all required beams present.
    std::vector<BearingCandidate> candidates;
    //! Diagnostics for missing beams or insufficient data.
    std::vector<ProcessingDiagnostic> diagnostics;

    /*!
     * \brief Checks whether the frame can be passed to bearing calculation.
     *
     * \return true when at least one candidate exists and no insufficient-data
     *         diagnostic is attached to the frame.
     */
    bool hasSufficientData() const noexcept;
};

/*!
 * \brief Full output of processing one sample or one batch.
 */
struct SampleProcessingResult
{
    //! Accepted samples with frequency-bin metadata.
    std::vector<ProcessedSample> acceptedSamples;
    //! Diagnostics collected across validation and frame building.
    std::vector<ProcessingDiagnostic> diagnostics;
    //! Aggregated frames grouped by band.
    std::vector<AggregatedBandFrame> aggregatedBandFrames;
    //! Waterfall-ready frame.
    WaterfallFrame waterfallFrame;
    //! Bearing-input frames grouped by band.
    std::vector<BearingInputFrame> bearingFrames;

    /*!
     * \brief Checks whether at least one sample passed validation.
     *
     * \return true when acceptedSamples is not empty.
     */
    bool hasAcceptedSamples() const noexcept;
    /*!
     * \brief Searches all nested diagnostics for a code.
     *
     * \param[in] code Diagnostic code to find.
     * \return true when the code exists anywhere in the result.
     */
    bool hasDiagnostic(ProcessingErrorCode code) const noexcept;
};

/*!
 * \brief Builds UI-independent waterfall rows from aggregated band frames.
 */
class WaterfallRowBuilder
{
public:
    /*!
     * \brief Converts aggregated band frames into waterfall rows.
     *
     * \param[in] frames Aggregated input frames.
     * \param[in] config Processing configuration and aggregation limits.
     * \return Prepared waterfall frame.
     */
    WaterfallFrame build(const std::vector<AggregatedBandFrame>& frames,
                         const SampleProcessingConfig& config) const;
};

/*!
 * \brief Builds intermediate bearing input frames from aggregated band frames.
 */
class BearingFrameBuilder
{
public:
    /*!
     * \brief Extracts complete per-bin beam candidates for bearing calculation.
     *
     * \param[in] frames Aggregated input frames.
     * \param[in] config Processing configuration and required beam set.
     * \return Bearing input frames grouped by band.
     */
    std::vector<BearingInputFrame> build(const std::vector<AggregatedBandFrame>& frames,
                                         const SampleProcessingConfig& config) const;
};

/*!
 * \brief Validates samples and prepares aggregation, waterfall, and bearing data.
 *
 * SampleProcessor belongs to the processing layer. It depends on core/domain
 * models only and must remain independent from QML, hardware sockets, and
 * storage implementations.
 */
class SampleProcessor
{
public:
    /*!
     * \brief Creates a processor with immutable processing configuration.
     *
     * \param[in] config Band and aggregation configuration.
     */
    explicit SampleProcessor(SampleProcessingConfig config);

    /*!
     * \brief Processes one signal sample.
     *
     * \param[in] sample Parsed domain sample.
     * \return Processing result for a single-sample batch.
     */
    SampleProcessingResult processSample(const core::SignalSample& sample);
    /*!
     * \brief Processes a batch of signal samples.
     *
     * Invalid samples are rejected diagnostically while valid samples continue
     * through aggregation and frame building.
     *
     * \param[in] batch Samples to process.
     * \return Full processing result.
     */
    SampleProcessingResult processBatch(const SampleBatch& batch);

    /*!
     * \brief Clears sequence tracking used for out-of-order diagnostics.
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
