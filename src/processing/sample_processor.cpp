#include "processing/sample_processor.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace siriusscope::processing {

namespace {

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

ProcessingDiagnostic makeSampleDiagnostic(ProcessingErrorCode code,
                                          ProcessingDiagnosticSeverity severity,
                                          std::string message,
                                          const core::SignalSample& sample)
{
    auto diagnostic = makeDiagnostic(code, severity, std::move(message));
    diagnostic.sampleIndex = sample.sampleIndex;
    diagnostic.bandIndex = sample.bandIndex;
    diagnostic.beamIndex = sample.beamIndex;
    diagnostic.frequencyHz = sample.absoluteFrequencyHz;
    return diagnostic;
}

std::size_t activeBeamSlotCount(const core::RuntimeCapabilities& capabilities)
{
    return static_cast<std::size_t>(std::max(0, capabilities.activeBeamCount));
}

std::size_t frequencyBinCount(const core::FrequencyRange& range, std::int64_t binWidthHz)
{
    if (binWidthHz <= 0 || range.maxHz < range.minHz) {
        return 0;
    }

    const auto widthHz = range.widthHz();
    if (widthHz <= 0) {
        return 1;
    }

    return static_cast<std::size_t>((widthHz + binWidthHz - 1) / binWidthHz);
}

core::FrequencyRange frequencyRangeForBin(const core::FrequencyRange& range,
                                          std::int64_t binWidthHz,
                                          std::size_t frequencyBin)
{
    const auto binStartHz = range.minHz
        + static_cast<std::int64_t>(frequencyBin) * binWidthHz;
    const auto binEndHz = std::min(range.maxHz, binStartHz + binWidthHz);
    return core::FrequencyRange{binStartHz, binEndHz};
}

std::optional<std::size_t> frequencyBinFor(const core::SignalSample& sample,
                                           const core::BandConfig& bandConfig,
                                           const AggregationWindow& window,
                                           ProcessingDiagnostic& diagnostic)
{
    const auto bandRange = bandConfig.frequencyRange();
    if (window.frequencyBinWidthHz <= 0) {
        diagnostic = makeSampleDiagnostic(ProcessingErrorCode::FrequencyBinOutOfRange,
                                          ProcessingDiagnosticSeverity::Error,
                                          "frequency bin width must be positive",
                                          sample);
        return std::nullopt;
    }

    if (!bandRange.contains(sample.absoluteFrequencyHz)) {
        diagnostic = makeSampleDiagnostic(ProcessingErrorCode::FrequencyBinOutOfRange,
                                          ProcessingDiagnosticSeverity::Error,
                                          "sample frequency is outside configured band range",
                                          sample);
        return std::nullopt;
    }

    const auto binCount = frequencyBinCount(bandRange, window.frequencyBinWidthHz);
    if (binCount == 0) {
        diagnostic = makeSampleDiagnostic(ProcessingErrorCode::FrequencyBinOutOfRange,
                                          ProcessingDiagnosticSeverity::Error,
                                          "band range cannot be divided into frequency bins",
                                          sample);
        return std::nullopt;
    }

    const auto offsetHz = sample.absoluteFrequencyHz - bandRange.minHz;
    auto bin = static_cast<std::size_t>(offsetHz / window.frequencyBinWidthHz);
    if (bin >= binCount && sample.absoluteFrequencyHz == bandRange.maxHz) {
        bin = binCount - 1;
    }

    if (bin >= binCount) {
        diagnostic = makeSampleDiagnostic(ProcessingErrorCode::FrequencyBinOutOfRange,
                                          ProcessingDiagnosticSeverity::Error,
                                          "sample frequency bin is outside configured band range",
                                          sample);
        return std::nullopt;
    }

    return bin;
}

bool containsDiagnostic(const std::vector<ProcessingDiagnostic>& diagnostics,
                        ProcessingErrorCode code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [code](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

void appendDiagnostics(std::vector<ProcessingDiagnostic>& destination,
                       const std::vector<ProcessingDiagnostic>& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

void updateBeamBin(AggregatedBeamBin& beamBin, int amplitude)
{
    if (beamBin.sampleCount == 0) {
        beamBin.minAmplitude = amplitude;
        beamBin.maxAmplitude = amplitude;
    } else {
        beamBin.minAmplitude = std::min(beamBin.minAmplitude, amplitude);
        beamBin.maxAmplitude = std::max(beamBin.maxAmplitude, amplitude);
    }

    ++beamBin.sampleCount;
    beamBin.amplitudeSum += static_cast<std::uint64_t>(amplitude);
}

WaterfallCell makeMissingCell(int bandIndex,
                              std::uint64_t sampleIndex,
                              std::size_t frequencyBin,
                              const core::FrequencyRange& frequencyRange,
                              const SampleProcessingConfig& config)
{
    WaterfallCell cell;
    cell.frequencyBin = frequencyBin;
    cell.frequencyRange = frequencyRange;
    cell.beamAmplitudes.assign(activeBeamSlotCount(config.capabilities), 0);
    cell.beamPresent.assign(activeBeamSlotCount(config.capabilities), false);
    cell.status = WaterfallCellStatus::MissingData;

    if (config.aggregationWindow.diagnoseMissingWaterfallCells) {
        auto diagnostic = makeDiagnostic(ProcessingErrorCode::MissingWaterfallData,
                                         ProcessingDiagnosticSeverity::Info,
                                         "waterfall frequency bin has no data");
        diagnostic.bandIndex = bandIndex;
        diagnostic.sampleIndex = sampleIndex;
        diagnostic.frequencyBin = frequencyBin;
        diagnostic.frequencyHz = frequencyRange.minHz;
        cell.diagnostics.push_back(std::move(diagnostic));
    }

    return cell;
}

WaterfallCell makeValidCell(const AggregatedFrequencyBin& bin,
                            const SampleProcessingConfig& config)
{
    WaterfallCell cell;
    cell.frequencyBin = bin.frequencyBin;
    cell.frequencyRange = bin.frequencyRange;
    cell.status = WaterfallCellStatus::Valid;
    cell.beamAmplitudes.assign(activeBeamSlotCount(config.capabilities), 0);
    cell.beamPresent.assign(activeBeamSlotCount(config.capabilities), false);

    std::uint64_t amplitudeSum = 0;
    std::size_t sampleCount = 0;
    for (const auto& beam : bin.beams) {
        if (beam.beamIndex >= 0
            && static_cast<std::size_t>(beam.beamIndex) < cell.beamAmplitudes.size()) {
            cell.beamAmplitudes[static_cast<std::size_t>(beam.beamIndex)] = beam.maxAmplitude;
            cell.beamPresent[static_cast<std::size_t>(beam.beamIndex)] = true;
        }

        cell.maxAmplitude = std::max(cell.maxAmplitude, beam.maxAmplitude);
        amplitudeSum += beam.amplitudeSum;
        sampleCount += beam.sampleCount;
    }

    if (sampleCount > 0) {
        cell.averageAmplitude = static_cast<double>(amplitudeSum) / static_cast<double>(sampleCount);
    }

    return cell;
}

bool requiredBeamsPresent(const BearingCandidate& candidate,
                          const std::vector<int>& requiredBeams)
{
    for (const auto beamIndex : requiredBeams) {
        if (!candidate.hasBeam(beamIndex)) {
            return false;
        }
    }

    return true;
}

struct DuplicateKey
{
    std::uint64_t sampleIndex = 0;
    int bandIndex = 0;
    int beamIndex = 0;
    std::int64_t frequencyHz = 0;

    bool operator<(const DuplicateKey& other) const noexcept
    {
        if (sampleIndex != other.sampleIndex) {
            return sampleIndex < other.sampleIndex;
        }
        if (bandIndex != other.bandIndex) {
            return bandIndex < other.bandIndex;
        }
        if (beamIndex != other.beamIndex) {
            return beamIndex < other.beamIndex;
        }
        return frequencyHz < other.frequencyHz;
    }
};

struct AggregateKey
{
    int bandIndex = 0;
    std::uint64_t sampleIndex = 0;
    std::size_t frequencyBin = 0;

    bool operator<(const AggregateKey& other) const noexcept
    {
        if (bandIndex != other.bandIndex) {
            return bandIndex < other.bandIndex;
        }
        if (sampleIndex != other.sampleIndex) {
            return sampleIndex < other.sampleIndex;
        }
        return frequencyBin < other.frequencyBin;
    }
};

} // namespace

double AggregatedBeamBin::averageAmplitude() const noexcept
{
    if (sampleCount == 0) {
        return 0.0;
    }

    return static_cast<double>(amplitudeSum) / static_cast<double>(sampleCount);
}

const AggregatedBeamBin* AggregatedFrequencyBin::beam(int beamIndex) const noexcept
{
    const auto found = std::find_if(beams.begin(), beams.end(), [beamIndex](const auto& beamBin) {
        return beamBin.beamIndex == beamIndex;
    });

    return found == beams.end() ? nullptr : &(*found);
}

bool WaterfallRow::hasMissingData() const noexcept
{
    return std::any_of(cells.begin(), cells.end(), [](const auto& cell) {
        return cell.status == WaterfallCellStatus::MissingData
            || cell.status == WaterfallCellStatus::InvalidData;
    });
}

bool BearingCandidate::hasBeam(int beamIndex) const noexcept
{
    if (beamIndex < 0 || static_cast<std::size_t>(beamIndex) >= beamPresent.size()) {
        return false;
    }

    return beamPresent[static_cast<std::size_t>(beamIndex)];
}

bool BearingInputFrame::hasSufficientData() const noexcept
{
    return !candidates.empty()
        && !containsDiagnostic(diagnostics, ProcessingErrorCode::InsufficientBearingData);
}

bool SampleProcessingResult::hasAcceptedSamples() const noexcept
{
    return !acceptedSamples.empty();
}

bool SampleProcessingResult::hasDiagnostic(ProcessingErrorCode code) const noexcept
{
    if (containsDiagnostic(diagnostics, code) || containsDiagnostic(waterfallFrame.diagnostics, code)) {
        return true;
    }

    for (const auto& row : waterfallFrame.rows) {
        if (containsDiagnostic(row.diagnostics, code)) {
            return true;
        }

        for (const auto& cell : row.cells) {
            if (containsDiagnostic(cell.diagnostics, code)) {
                return true;
            }
        }
    }

    for (const auto& frame : bearingFrames) {
        if (containsDiagnostic(frame.diagnostics, code)) {
            return true;
        }
    }

    for (const auto& frame : aggregatedBandFrames) {
        if (containsDiagnostic(frame.diagnostics, code)) {
            return true;
        }

        for (const auto& bin : frame.bins) {
            if (containsDiagnostic(bin.diagnostics, code)) {
                return true;
            }
        }
    }

    return false;
}

WaterfallFrame WaterfallRowBuilder::build(const std::vector<AggregatedBandFrame>& frames,
                                          const SampleProcessingConfig& config) const
{
    WaterfallFrame waterfallFrame;

    for (const auto& frame : frames) {
        const auto binCount = frequencyBinCount(frame.frequencyRange,
                                                config.aggregationWindow.frequencyBinWidthHz);
        if (binCount == 0) {
            auto diagnostic = makeDiagnostic(ProcessingErrorCode::FrequencyBinOutOfRange,
                                             ProcessingDiagnosticSeverity::Error,
                                             "waterfall frame has no usable frequency bins");
            diagnostic.bandIndex = frame.bandIndex;
            waterfallFrame.diagnostics.push_back(std::move(diagnostic));
            continue;
        }

        std::map<std::uint64_t, std::map<std::size_t, const AggregatedFrequencyBin*>> rowsBySample;
        for (const auto& bin : frame.bins) {
            rowsBySample[bin.sampleIndex][bin.frequencyBin] = &bin;
        }

        for (const auto& [sampleIndex, binsByFrequency] : rowsBySample) {
            WaterfallRow row;
            row.bandIndex = frame.bandIndex;
            row.sampleIndexStart = sampleIndex;
            row.sampleIndexEnd = sampleIndex;
            row.frequencyRange = frame.frequencyRange;
            row.cells.reserve(binCount);

            bool hasMissingData = false;
            for (std::size_t frequencyBin = 0; frequencyBin < binCount; ++frequencyBin) {
                const auto frequencyRange = frequencyRangeForBin(frame.frequencyRange,
                                                                 config.aggregationWindow
                                                                     .frequencyBinWidthHz,
                                                                 frequencyBin);
                const auto found = binsByFrequency.find(frequencyBin);
                if (found == binsByFrequency.end()) {
                    hasMissingData = true;
                    row.cells.push_back(makeMissingCell(frame.bandIndex,
                                                        sampleIndex,
                                                        frequencyBin,
                                                        frequencyRange,
                                                        config));
                    continue;
                }

                row.cells.push_back(makeValidCell(*found->second, config));
            }

            if (hasMissingData && config.aggregationWindow.diagnoseMissingWaterfallCells) {
                auto diagnostic = makeDiagnostic(ProcessingErrorCode::MissingWaterfallData,
                                                 ProcessingDiagnosticSeverity::Info,
                                                 "waterfall row contains empty frequency bins");
                diagnostic.bandIndex = frame.bandIndex;
                diagnostic.sampleIndex = sampleIndex;
                row.diagnostics.push_back(diagnostic);
                waterfallFrame.diagnostics.push_back(std::move(diagnostic));
            }

            waterfallFrame.rows.push_back(std::move(row));
        }
    }

    return waterfallFrame;
}

std::vector<BearingInputFrame> BearingFrameBuilder::build(
    const std::vector<AggregatedBandFrame>& frames,
    const SampleProcessingConfig& config) const
{
    std::vector<BearingInputFrame> bearingFrames;

    for (const auto& frame : frames) {
        BearingInputFrame bearingFrame;
        bearingFrame.bandIndex = frame.bandIndex;
        bearingFrame.sampleIndexStart = frame.sampleIndexStart;
        bearingFrame.sampleIndexEnd = frame.sampleIndexEnd;

        for (const auto& bin : frame.bins) {
            BearingCandidate candidate;
            candidate.bandIndex = bin.bandIndex;
            candidate.sampleIndexStart = bin.sampleIndex;
            candidate.sampleIndexEnd = bin.sampleIndex;
            candidate.frequencyBin = bin.frequencyBin;
            candidate.frequencyRange = bin.frequencyRange;
            candidate.beamAmplitudes.assign(activeBeamSlotCount(config.capabilities), 0);
            candidate.beamPresent.assign(activeBeamSlotCount(config.capabilities), false);

            for (const auto& beam : bin.beams) {
                if (beam.beamIndex >= 0
                    && static_cast<std::size_t>(beam.beamIndex) < candidate.beamAmplitudes.size()) {
                    candidate.beamAmplitudes[static_cast<std::size_t>(beam.beamIndex)] =
                        beam.maxAmplitude;
                    candidate.beamPresent[static_cast<std::size_t>(beam.beamIndex)] = true;
                }
            }

            if (requiredBeamsPresent(candidate, config.aggregationWindow.requiredBearingBeams)) {
                bearingFrame.candidates.push_back(std::move(candidate));
                continue;
            }

            auto missingBeam = makeDiagnostic(ProcessingErrorCode::MissingBeamSample,
                                              ProcessingDiagnosticSeverity::Warning,
                                              "bearing input candidate lacks a required beam");
            missingBeam.bandIndex = bin.bandIndex;
            missingBeam.sampleIndex = bin.sampleIndex;
            missingBeam.frequencyBin = bin.frequencyBin;
            missingBeam.frequencyHz = bin.frequencyRange.minHz;
            bearingFrame.diagnostics.push_back(std::move(missingBeam));
        }

        if (bearingFrame.candidates.empty()) {
            auto insufficient = makeDiagnostic(ProcessingErrorCode::InsufficientBearingData,
                                               ProcessingDiagnosticSeverity::Warning,
                                               "bearing input frame has no complete two-beam candidate");
            insufficient.bandIndex = frame.bandIndex;
            insufficient.sampleIndex = frame.sampleIndexStart;
            bearingFrame.diagnostics.push_back(std::move(insufficient));
        }

        bearingFrames.push_back(std::move(bearingFrame));
    }

    return bearingFrames;
}

SampleProcessor::SampleProcessor(SampleProcessingConfig config)
    : m_config(std::move(config))
{
}

SampleProcessingResult SampleProcessor::processSample(const core::SignalSample& sample)
{
    return processBatch(SampleBatch{{sample}});
}

SampleProcessingResult SampleProcessor::processBatch(const SampleBatch& batch)
{
    SampleProcessingResult result;

    if (batch.samples.empty()) {
        result.diagnostics.push_back(makeDiagnostic(ProcessingErrorCode::EmptyBatch,
                                                    ProcessingDiagnosticSeverity::Info,
                                                    "sample batch is empty"));
        return result;
    }

    if (batch.samples.size() > m_config.aggregationWindow.maxSamplesPerBatch) {
        result.diagnostics.push_back(
            makeDiagnostic(ProcessingErrorCode::AggregationWindowOverflow,
                           ProcessingDiagnosticSeverity::Warning,
                           "sample batch exceeds configured aggregation window"));
    }

    std::set<DuplicateKey> seenSamples;
    for (const auto& sample : batch.samples) {
        const core::BandConfig* bandConfig = nullptr;
        auto validation = validateSample(sample, bandConfig);
        if (!validation) {
            auto diagnostic = makeSampleDiagnostic(ProcessingErrorCode::InvalidSampleRejected,
                                                  ProcessingDiagnosticSeverity::Error,
                                                  "invalid domain sample rejected",
                                                  sample);
            diagnostic.domainIssues = validation.issues();
            result.diagnostics.push_back(std::move(diagnostic));
            continue;
        }

        auto processed = prepareSample(sample, *bandConfig, result);
        if (!processed) {
            continue;
        }

        const auto& acceptedSample = processed->sample;
        const DuplicateKey duplicateKey{acceptedSample.sampleIndex,
                                        acceptedSample.bandIndex,
                                        acceptedSample.beamIndex,
                                        acceptedSample.absoluteFrequencyHz};
        if (!seenSamples.insert(duplicateKey).second) {
            result.diagnostics.push_back(makeSampleDiagnostic(ProcessingErrorCode::DuplicateSample,
                                                              ProcessingDiagnosticSeverity::Warning,
                                                              "duplicate sample detected",
                                                              acceptedSample));
        }

        if (m_highestAcceptedSampleIndex
            && acceptedSample.sampleIndex < *m_highestAcceptedSampleIndex) {
            result.diagnostics.push_back(
                makeSampleDiagnostic(ProcessingErrorCode::OutOfOrderSampleIndex,
                                     ProcessingDiagnosticSeverity::Warning,
                                     "sample index is lower than an already accepted sample index",
                                     acceptedSample));
        }
        if (!m_highestAcceptedSampleIndex
            || acceptedSample.sampleIndex > *m_highestAcceptedSampleIndex) {
            m_highestAcceptedSampleIndex = acceptedSample.sampleIndex;
        }

        result.acceptedSamples.push_back(std::move(*processed));
    }

    result.aggregatedBandFrames = aggregate(result.acceptedSamples);
    result.waterfallFrame = WaterfallRowBuilder{}.build(result.aggregatedBandFrames, m_config);
    result.bearingFrames = BearingFrameBuilder{}.build(result.aggregatedBandFrames, m_config);

    appendDiagnostics(result.diagnostics, result.waterfallFrame.diagnostics);
    for (const auto& frame : result.bearingFrames) {
        appendDiagnostics(result.diagnostics, frame.diagnostics);
    }

    return result;
}

void SampleProcessor::resetSequenceTracking() noexcept
{
    m_highestAcceptedSampleIndex.reset();
}

const core::BandConfig* SampleProcessor::bandConfigFor(int bandIndex) const noexcept
{
    const auto found = std::find_if(m_config.bands.begin(), m_config.bands.end(), [bandIndex](const auto& band) {
        return band.bandIndex == bandIndex;
    });

    return found == m_config.bands.end() ? nullptr : &(*found);
}

core::ValidationResult SampleProcessor::validateSample(const core::SignalSample& sample,
                                                       const core::BandConfig*& bandConfig) const
{
    bandConfig = bandConfigFor(sample.bandIndex);
    if (bandConfig) {
        return sample.validate(*bandConfig, m_config.capabilities);
    }

    auto validation = core::validateBandIndex(sample.bandIndex, m_config.capabilities);
    validation.merge(core::validateAmplitude(sample.amplitude));
    validation.merge(core::validateBeamIndex(sample.beamIndex, m_config.capabilities));
    validation.merge(core::validateSystemFrequency(sample.absoluteFrequencyHz));
    if (!validation.contains(core::ValidationCode::InvalidBandIndex)) {
        validation.add(core::ValidationCode::InvalidBandIndex,
                       "sample band has no processing band config");
    }

    return validation;
}

std::optional<ProcessedSample> SampleProcessor::prepareSample(const core::SignalSample& sample,
                                                              const core::BandConfig& bandConfig,
                                                              SampleProcessingResult& result) const
{
    ProcessingDiagnostic diagnostic;
    auto frequencyBin = frequencyBinFor(sample, bandConfig, m_config.aggregationWindow, diagnostic);
    if (!frequencyBin) {
        result.diagnostics.push_back(std::move(diagnostic));
        return std::nullopt;
    }

    return ProcessedSample{
        sample,
        *frequencyBin,
        frequencyRangeForBin(bandConfig.frequencyRange(),
                             m_config.aggregationWindow.frequencyBinWidthHz,
                             *frequencyBin),
    };
}

std::vector<AggregatedBandFrame> SampleProcessor::aggregate(
    const std::vector<ProcessedSample>& samples) const
{
    std::map<AggregateKey, AggregatedFrequencyBin> bins;
    for (const auto& processed : samples) {
        const AggregateKey key{processed.sample.bandIndex,
                               processed.sample.sampleIndex,
                               processed.frequencyBin};
        auto& bin = bins[key];
        if (bin.beams.empty() && bin.frequencyRange.widthHz() == 0) {
            bin.bandIndex = processed.sample.bandIndex;
            bin.sampleIndex = processed.sample.sampleIndex;
            bin.frequencyBin = processed.frequencyBin;
            bin.frequencyRange = processed.frequencyRange;
        }

        auto beam = std::find_if(bin.beams.begin(), bin.beams.end(), [&processed](const auto& beamBin) {
            return beamBin.beamIndex == processed.sample.beamIndex;
        });
        if (beam == bin.beams.end()) {
            AggregatedBeamBin beamBin;
            beamBin.beamIndex = processed.sample.beamIndex;
            updateBeamBin(beamBin, processed.sample.amplitude);
            bin.beams.push_back(std::move(beamBin));
        } else {
            updateBeamBin(*beam, processed.sample.amplitude);
        }
    }

    std::map<int, AggregatedBandFrame> framesByBand;
    for (const auto& [key, bin] : bins) {
        auto& frame = framesByBand[key.bandIndex];
        if (frame.bins.empty()) {
            frame.bandIndex = key.bandIndex;
            frame.sampleIndexStart = key.sampleIndex;
            frame.sampleIndexEnd = key.sampleIndex;
            if (const auto* bandConfig = bandConfigFor(key.bandIndex)) {
                frame.frequencyRange = bandConfig->frequencyRange();
            }
        } else {
            frame.sampleIndexStart = std::min(frame.sampleIndexStart, key.sampleIndex);
            frame.sampleIndexEnd = std::max(frame.sampleIndexEnd, key.sampleIndex);
        }

        frame.bins.push_back(bin);
    }

    std::vector<AggregatedBandFrame> frames;
    frames.reserve(framesByBand.size());
    for (auto& [_, frame] : framesByBand) {
        frames.push_back(std::move(frame));
    }

    return frames;
}

} // namespace siriusscope::processing
