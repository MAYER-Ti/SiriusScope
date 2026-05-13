#include "processing/sample_processor.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace siriusscope::core;
using namespace siriusscope::processing;

class TestRunner
{
public:
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            ++m_failed;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

std::vector<BandConfig> makeBandConfigs()
{
    std::vector<BandConfig> bands;
    bands.reserve(DomainConstraints::currentBandCount);
    for (int bandIndex = 0; bandIndex < DomainConstraints::currentBandCount; ++bandIndex) {
        const auto centerHz = 550'000'000LL
            + static_cast<std::int64_t>(bandIndex) * DomainConstraints::maxBandWidthHz;
        auto created = BandConfig::create(bandIndex, centerHz, DomainConstraints::maxBandWidthHz);
        bands.push_back(*created.value());
    }

    return bands;
}

SampleProcessingConfig makeConfig()
{
    SampleProcessingConfig config;
    config.bands = makeBandConfigs();
    config.aggregationWindow.frequencyBinWidthHz = 100'000'000LL;
    return config;
}

SignalSample makeSample(const SampleProcessingConfig& config,
                        std::uint64_t sampleIndex,
                        int bandIndex,
                        int beamIndex,
                        std::int64_t frequencyOffsetHz = 0,
                        int amplitude = 32)
{
    const auto& band = config.bands[static_cast<std::size_t>(bandIndex)];
    auto created = SignalSample::create(BeamSample{sampleIndex, frequencyOffsetHz, amplitude, beamIndex},
                                        band,
                                        config.capabilities);
    return *created.value();
}

bool hasDomainIssue(const SampleProcessingResult& result, ValidationCode code)
{
    for (const auto& diagnostic : result.diagnostics) {
        for (const auto& issue : diagnostic.domainIssues) {
            if (issue.code == code) {
                return true;
            }
        }
    }

    return false;
}

const AggregatedBandFrame* findBandFrame(const SampleProcessingResult& result, int bandIndex)
{
    const auto found = std::find_if(result.aggregatedBandFrames.begin(),
                                    result.aggregatedBandFrames.end(),
                                    [bandIndex](const auto& frame) {
                                        return frame.bandIndex == bandIndex;
                                    });
    return found == result.aggregatedBandFrames.end() ? nullptr : &(*found);
}

const WaterfallRow* findWaterfallRow(const SampleProcessingResult& result, int bandIndex)
{
    const auto found = std::find_if(result.waterfallFrame.rows.begin(),
                                    result.waterfallFrame.rows.end(),
                                    [bandIndex](const auto& row) {
                                        return row.bandIndex == bandIndex;
                                    });
    return found == result.waterfallFrame.rows.end() ? nullptr : &(*found);
}

bool hasValidWaterfallCell(const WaterfallRow& row)
{
    return std::any_of(row.cells.begin(), row.cells.end(), [](const auto& cell) {
        return cell.status == WaterfallCellStatus::Valid;
    });
}

bool hasMissingWaterfallCell(const WaterfallRow& row)
{
    return std::any_of(row.cells.begin(), row.cells.end(), [](const auto& cell) {
        return cell.status == WaterfallCellStatus::MissingData;
    });
}

void testValidSamplePassesProcessing(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processSample(makeSample(config, 1, 0, 0));

    test.require(result.hasAcceptedSamples(), "valid sample is accepted");
    test.require(result.acceptedSamples.size() == 1, "one valid sample is preserved");
    test.require(!result.hasDiagnostic(ProcessingErrorCode::InvalidSampleRejected),
                 "valid sample has no rejection diagnostic");
}

void testInvalidAmplitudeRejected(TestRunner& test)
{
    const auto config = makeConfig();
    auto sample = makeSample(config, 1, 0, 0);
    sample.amplitude = 0;
    SampleProcessor processor(config);

    const auto result = processor.processSample(sample);

    test.require(!result.hasAcceptedSamples(), "invalid amplitude sample is rejected");
    test.require(result.hasDiagnostic(ProcessingErrorCode::InvalidSampleRejected),
                 "invalid amplitude produces processing diagnostic");
    test.require(hasDomainIssue(result, ValidationCode::InvalidAmplitude),
                 "invalid amplitude uses domain validation issue");
}

void testInvalidBeamRejected(TestRunner& test)
{
    const auto config = makeConfig();
    auto sample = makeSample(config, 1, 0, 0);
    sample.beamIndex = 2;
    SampleProcessor processor(config);

    const auto result = processor.processSample(sample);

    test.require(!result.hasAcceptedSamples(), "invalid beam sample is rejected");
    test.require(hasDomainIssue(result, ValidationCode::InvalidBeamIndex),
                 "invalid beam uses domain validation issue");
}

void testInvalidFrequencyAndBandRejected(TestRunner& test)
{
    const auto config = makeConfig();
    auto invalidFrequency = makeSample(config, 1, 0, 0);
    invalidFrequency.absoluteFrequencyHz = DomainConstraints::minSystemFrequencyHz - 1;

    auto invalidBand = makeSample(config, 2, 0, 0);
    invalidBand.bandIndex = DomainConstraints::currentBandCount;

    SampleProcessor processor(config);
    const auto result = processor.processBatch(SampleBatch{{invalidFrequency, invalidBand}});

    test.require(!result.hasAcceptedSamples(), "invalid frequency and band samples are rejected");
    test.require(hasDomainIssue(result, ValidationCode::InvalidFrequency),
                 "invalid frequency uses domain validation issue");
    test.require(hasDomainIssue(result, ValidationCode::InvalidBandIndex),
                 "invalid band uses domain validation issue");
}

void testPartialInvalidBatch(TestRunner& test)
{
    const auto config = makeConfig();
    auto invalid = makeSample(config, 2, 0, 0);
    invalid.amplitude = 128;

    SampleProcessor processor(config);
    const auto result = processor.processBatch(
        SampleBatch{{makeSample(config, 1, 0, 0), invalid, makeSample(config, 3, 1, 0)}});

    test.require(result.acceptedSamples.size() == 2, "partial invalid batch keeps valid samples");
    test.require(result.hasDiagnostic(ProcessingErrorCode::InvalidSampleRejected),
                 "partial invalid batch reports rejection diagnostic");
}

void testGroupingByBandIndex(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processBatch(
        SampleBatch{{makeSample(config, 1, 0, 0), makeSample(config, 1, 1, 0)}});

    test.require(findBandFrame(result, 0) != nullptr, "aggregation has band 0 frame");
    test.require(findBandFrame(result, 1) != nullptr, "aggregation has band 1 frame");
}

void testGroupingByBeamIndex(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processBatch(
        SampleBatch{{makeSample(config, 1, 0, 0), makeSample(config, 1, 0, 1)}});
    const auto* frame = findBandFrame(result, 0);

    test.require(frame != nullptr && !frame->bins.empty(), "aggregation has band bins");
    test.require(frame != nullptr && frame->bins.front().beam(0) != nullptr, "aggregation keeps beam 0");
    test.require(frame != nullptr && frame->bins.front().beam(1) != nullptr, "aggregation keeps beam 1");
}

void testSampleIndexPreserved(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processSample(makeSample(config, 42, 0, 0));
    const auto* row = findWaterfallRow(result, 0);

    test.require(result.acceptedSamples.front().sample.sampleIndex == 42,
                 "processed sample preserves sampleIndex");
    test.require(row != nullptr && row->sampleIndexStart == 42 && row->sampleIndexEnd == 42,
                 "waterfall row preserves sampleIndex");
}

void testOutOfOrderSampleIndexDiagnostic(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processBatch(
        SampleBatch{{makeSample(config, 10, 0, 0), makeSample(config, 9, 0, 1)}});

    test.require(result.hasDiagnostic(ProcessingErrorCode::OutOfOrderSampleIndex),
                 "out-of-order sample index is diagnosed");
}

void testDuplicateSampleDiagnostic(TestRunner& test)
{
    const auto config = makeConfig();
    const auto sample = makeSample(config, 10, 0, 0);
    SampleProcessor processor(config);

    const auto result = processor.processBatch(SampleBatch{{sample, sample}});

    test.require(result.hasDiagnostic(ProcessingErrorCode::DuplicateSample),
                 "duplicate sample is diagnosed");
}

void testWaterfallRowCreated(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processSample(makeSample(config, 1, 0, 0));
    const auto* row = findWaterfallRow(result, 0);

    test.require(row != nullptr, "waterfall row is created");
    test.require(row != nullptr && row->bandIndex == 0, "waterfall row preserves bandIndex");
    test.require(row != nullptr && hasValidWaterfallCell(*row), "waterfall row contains valid cell");
}

void testMissingWaterfallDataDiagnosed(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processSample(makeSample(config, 1, 0, 0));
    const auto* row = findWaterfallRow(result, 0);

    test.require(row != nullptr && hasMissingWaterfallCell(*row), "waterfall row marks missing bins");
    test.require(result.hasDiagnostic(ProcessingErrorCode::MissingWaterfallData),
                 "missing waterfall data is diagnosed");
}

void testBearingFrameCreatedForTwoBeams(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processBatch(
        SampleBatch{{makeSample(config, 1, 0, 0), makeSample(config, 1, 0, 1)}});

    test.require(!result.bearingFrames.empty(), "bearing input frame is created");
    test.require(!result.bearingFrames.empty() && result.bearingFrames.front().hasSufficientData(),
                 "bearing frame has sufficient two-beam data");
    test.require(!result.bearingFrames.empty() && result.bearingFrames.front().candidates.size() == 1,
                 "bearing frame has one complete candidate");
}

void testMissingBearingBeamDiagnosed(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processSample(makeSample(config, 1, 0, 0));

    test.require(result.hasDiagnostic(ProcessingErrorCode::MissingBeamSample),
                 "missing bearing beam is diagnosed");
    test.require(result.hasDiagnostic(ProcessingErrorCode::InsufficientBearingData),
                 "single-beam frame has insufficient bearing data");
}

void testEmptyBatchDiagnostic(TestRunner& test)
{
    const auto config = makeConfig();
    SampleProcessor processor(config);

    const auto result = processor.processBatch(SampleBatch{});

    test.require(!result.hasAcceptedSamples(), "empty batch has no accepted samples");
    test.require(result.hasDiagnostic(ProcessingErrorCode::EmptyBatch), "empty batch is diagnosed");
}

} // namespace

int main()
{
    TestRunner test;

    testValidSamplePassesProcessing(test);
    testInvalidAmplitudeRejected(test);
    testInvalidBeamRejected(test);
    testInvalidFrequencyAndBandRejected(test);
    testPartialInvalidBatch(test);
    testGroupingByBandIndex(test);
    testGroupingByBeamIndex(test);
    testSampleIndexPreserved(test);
    testOutOfOrderSampleIndexDiagnostic(test);
    testDuplicateSampleDiagnostic(test);
    testWaterfallRowCreated(test);
    testMissingWaterfallDataDiagnosed(test);
    testBearingFrameCreatedForTwoBeams(test);
    testMissingBearingBeamDiagnosed(test);
    testEmptyBatchDiagnostic(test);

    return test.result();
}
