#include "processing/signal_parameter_accumulator.h"

#include <algorithm>
#include <cmath>
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

bool nearly(double actual, double expected, double tolerance = 0.0001)
{
    return std::abs(actual - expected) <= tolerance;
}

SignalSample makeSample(std::uint64_t sampleIndex,
                        int bandIndex = 0,
                        std::int64_t frequencyHz = 1'000'000'000LL,
                        int amplitude = 32,
                        int beamIndex = 0)
{
    return SignalSample{sampleIndex, bandIndex, 0, frequencyHz, amplitude, beamIndex};
}

const SignalParameters* findParameters(const std::vector<SignalParameters>& parameters,
                                       int bandIndex)
{
    const auto found =
        std::find_if(parameters.cbegin(), parameters.cend(), [bandIndex](const auto& item) {
            return item.bandIndex == bandIndex;
        });
    return found == parameters.cend() ? nullptr : &(*found);
}

bool sameParameters(const SignalParameters& lhs, const SignalParameters& rhs)
{
    const bool samePri =
        (!lhs.pulseRepetitionPeriodUs && !rhs.pulseRepetitionPeriodUs)
        || (lhs.pulseRepetitionPeriodUs && rhs.pulseRepetitionPeriodUs
            && nearly(*lhs.pulseRepetitionPeriodUs, *rhs.pulseRepetitionPeriodUs));
    return lhs.bandIndex == rhs.bandIndex && lhs.pulseCount == rhs.pulseCount
        && nearly(lhs.pulseWidthUs, rhs.pulseWidthUs) && samePri
        && lhs.frequenciesHz == rhs.frequenciesHz;
}

SignalParameterEstimatorConfig microsecondSampleConfig()
{
    SignalParameterEstimatorConfig config;
    config.samplePeriodNs = 1000;
    config.maxIntraPulseGapSamples = 1;
    return config;
}

SignalParameterEstimatorConfig streamingMicrosecondSampleConfig()
{
    auto config = microsecondSampleConfig();
    config.ingestMode = SignalParameterIngestMode::Streaming;
    return config;
}

SignalParameterEstimatorConfig trustedStreamingVectorMicrosecondSampleConfig(
    std::size_t bandStateCapacity = 5)
{
    auto config = streamingMicrosecondSampleConfig();
    config.validationMode = SignalParameterValidationMode::TrustedValidatedSamples;
    config.bandStateMode = SignalParameterBandStateMode::FixedBandIndexVector;
    config.bandStateCapacity = bandStateCapacity;
    return config;
}

void testEmptyAccumulatorReturnsNoParameters(TestRunner& test)
{
    const SignalParameterAccumulator accumulator;

    test.require(accumulator.finalize().empty(), "empty accumulator returns no parameters");
    test.require(accumulator.acceptedSampleCount() == 0,
                 "empty accumulator has no accepted samples");
    test.require(accumulator.rejectedSampleCount() == 0,
                 "empty accumulator has no rejected samples");
    test.require(accumulator.pulseCount() == 0, "empty accumulator has no pulses");
}

void testOnePulseGivesPulseWidthWithoutPri(TestRunner& test)
{
    SignalParameterAccumulator accumulator(microsecondSampleConfig());
    accumulator.ingest({makeSample(10), makeSample(11), makeSample(12)});

    const auto result = accumulator.finalize();

    test.require(result.size() == 1, "one pulse produces one band estimate");
    test.require(accumulator.acceptedSampleCount() == 3, "one pulse accepts all samples");
    test.require(accumulator.pulseCount() == 1, "one pulse is counted");
    if (result.empty()) {
        return;
    }

    test.require(result.front().pulseCount == 1, "one pulse count is preserved");
    test.require(nearly(result.front().pulseWidthUs, 3.0), "one pulse PW is calculated");
    test.require(!result.front().pulseRepetitionPeriodUs, "one pulse has no PRI");
}

void testTwoPulsesGiveAveragePulseWidthAndPri(TestRunner& test)
{
    SignalParameterAccumulator accumulator(microsecondSampleConfig());
    accumulator.ingest({makeSample(10),
                        makeSample(11),
                        makeSample(20),
                        makeSample(21),
                        makeSample(22)});

    const auto result = accumulator.finalize();

    test.require(result.size() == 1, "two pulses produce one band estimate");
    if (result.empty()) {
        return;
    }

    test.require(result.front().pulseCount == 2, "two pulses are counted");
    test.require(nearly(result.front().pulseWidthUs, 2.5), "average PW is calculated");
    test.require(result.front().pulseRepetitionPeriodUs
                     && nearly(*result.front().pulseRepetitionPeriodUs, 10.0),
                 "average PRI is calculated");
}

void testStreamingBatchesMatchOneBatch(TestRunner& test)
{
    const std::vector<SignalSample> batch1{makeSample(10), makeSample(11)};
    const std::vector<SignalSample> batch2{makeSample(20), makeSample(21), makeSample(22)};
    const std::vector<SignalSample> allSamples{makeSample(10),
                                               makeSample(11),
                                               makeSample(20),
                                               makeSample(21),
                                               makeSample(22)};

    SignalParameterAccumulator streaming(microsecondSampleConfig());
    streaming.ingest(batch1);
    streaming.ingest(batch2);

    SignalParameterAccumulator oneBatch(microsecondSampleConfig());
    oneBatch.ingest(allSamples);

    const auto streamingResult = streaming.finalize();
    const auto oneBatchResult = oneBatch.finalize();

    test.require(streamingResult.size() == oneBatchResult.size(),
                 "streaming and one-batch result sizes match");
    test.require(!streamingResult.empty() && !oneBatchResult.empty()
                     && sameParameters(streamingResult.front(), oneBatchResult.front()),
                 "streaming batches produce the same parameters as one batch");
}

void testBandsAreCalculatedIndependently(TestRunner& test)
{
    SignalParameterAccumulator accumulator(microsecondSampleConfig());
    accumulator.ingest({makeSample(10, 0),
                        makeSample(11, 0),
                        makeSample(20, 0),
                        makeSample(100, 1),
                        makeSample(101, 1),
                        makeSample(102, 1)});

    const auto result = accumulator.finalize();
    const auto* band0 = findParameters(result, 0);
    const auto* band1 = findParameters(result, 1);

    test.require(result.size() == 2, "two valid bands produce two estimates");
    test.require(band0 != nullptr, "band 0 estimate exists");
    test.require(band1 != nullptr, "band 1 estimate exists");
    if (band0) {
        test.require(band0->pulseCount == 2, "band 0 pulse count is independent");
        test.require(nearly(band0->pulseWidthUs, 1.5), "band 0 PW is independent");
        test.require(band0->pulseRepetitionPeriodUs
                         && nearly(*band0->pulseRepetitionPeriodUs, 10.0),
                     "band 0 PRI is independent");
    }
    if (band1) {
        test.require(band1->pulseCount == 1, "band 1 pulse count is independent");
        test.require(nearly(band1->pulseWidthUs, 3.0), "band 1 PW is independent");
        test.require(!band1->pulseRepetitionPeriodUs, "band 1 has no PRI");
    }
}

void testStreamingEqualsSortedForMonotonicPerBandInput(TestRunner& test)
{
    const std::vector<SignalSample> samples{
        makeSample(10, 0, 1'000'000'000LL),
        makeSample(11, 0, 1'000'000'000LL),
        makeSample(12, 0, 1'000'000'000LL),
        makeSample(15, 1, 1'500'000'000LL),
        makeSample(16, 1, 1'500'000'000LL),
        makeSample(30, 0, 1'100'000'000LL),
        makeSample(31, 0, 1'100'000'000LL),
        makeSample(40, 1, 1'600'000'000LL),
        makeSample(41, 1, 1'600'000'000LL),
        makeSample(42, 1, 1'600'000'000LL),
    };

    SignalParameterAccumulator sorted(microsecondSampleConfig());
    sorted.ingest(samples);

    SignalParameterAccumulator streaming(streamingMicrosecondSampleConfig());
    streaming.ingest(samples);

    const auto sortedResult = sorted.finalize();
    const auto streamingResult = streaming.finalize();

    test.require(sortedResult.size() == streamingResult.size(),
                 "streaming and sorted monotonic result sizes match");
    for (int bandIndex = 0; bandIndex < 2; ++bandIndex) {
        const auto* sortedBand = findParameters(sortedResult, bandIndex);
        const auto* streamingBand = findParameters(streamingResult, bandIndex);
        test.require(sortedBand != nullptr, "sorted monotonic band estimate exists");
        test.require(streamingBand != nullptr, "streaming monotonic band estimate exists");
        if (sortedBand && streamingBand) {
            test.require(sameParameters(*sortedBand, *streamingBand),
                         "streaming monotonic band parameters match sorted mode");
        }
    }
}

void testStreamingRejectsOutOfOrderSamplePerBand(TestRunner& test)
{
    SignalParameterAccumulator accumulator(streamingMicrosecondSampleConfig());
    accumulator.ingest({makeSample(10), makeSample(12), makeSample(11)});

    test.require(accumulator.acceptedSampleCount() == 2,
                 "streaming accepts only monotonic per-band samples");
    test.require(accumulator.rejectedSampleCount() == 1,
                 "streaming rejects out-of-order per-band sample");
}

void testTrustedStreamingVectorMatchesValidatedStreaming(TestRunner& test)
{
    const std::vector<SignalSample> samples{
        makeSample(10, 0, 1'000'000'000LL),
        makeSample(11, 0, 1'000'000'000LL),
        makeSample(30, 0, 1'100'000'000LL),
        makeSample(31, 0, 1'100'000'000LL),
        makeSample(12, 1, 1'500'000'000LL),
        makeSample(13, 1, 1'500'000'000LL),
        makeSample(40, 1, 1'600'000'000LL),
        makeSample(41, 1, 1'600'000'000LL),
    };

    SignalParameterAccumulator validated(streamingMicrosecondSampleConfig());
    validated.ingest(samples);

    SignalParameterAccumulator trusted(trustedStreamingVectorMicrosecondSampleConfig());
    trusted.ingest(samples);

    const auto validatedResult = validated.finalize();
    const auto trustedResult = trusted.finalize();

    test.require(validatedResult.size() == trustedResult.size(),
                 "trusted vector result count matches validated streaming");
    test.require(validated.acceptedSampleCount() == trusted.acceptedSampleCount(),
                 "trusted vector accepted count matches validated streaming");
    test.require(validated.rejectedSampleCount() == 0,
                 "validated streaming rejects no valid trusted-equivalence samples");
    test.require(trusted.rejectedSampleCount() == 0,
                 "trusted vector rejects no valid trusted-equivalence samples");

    for (int bandIndex = 0; bandIndex < 2; ++bandIndex) {
        const auto* validatedBand = findParameters(validatedResult, bandIndex);
        const auto* trustedBand = findParameters(trustedResult, bandIndex);
        test.require(validatedBand != nullptr, "validated trusted-equivalence band exists");
        test.require(trustedBand != nullptr, "trusted vector trusted-equivalence band exists");
        if (validatedBand && trustedBand) {
            test.require(sameParameters(*validatedBand, *trustedBand),
                         "trusted vector parameters match validated streaming");
        }
    }
}

void testTrustedVectorRejectsOutOfCapacityBand(TestRunner& test)
{
    SignalParameterAccumulator accumulator(
        trustedStreamingVectorMicrosecondSampleConfig(2));
    accumulator.ingest({makeSample(10, 0), makeSample(11, 3)});

    test.require(accumulator.acceptedSampleCount() == 1,
                 "trusted vector accepts in-capacity band sample");
    test.require(accumulator.rejectedSampleCount() == 1,
                 "trusted vector rejects out-of-capacity band sample");
}

void testTrustedMapRejectsOutOfRangeBand(TestRunner& test)
{
    auto config = streamingMicrosecondSampleConfig();
    config.validationMode = SignalParameterValidationMode::TrustedValidatedSamples;
    config.bandStateMode = SignalParameterBandStateMode::MapByBandIndex;

    SignalParameterAccumulator accumulator(config);
    accumulator.ingest({makeSample(10, 0), makeSample(11, 99)});

    test.require(accumulator.acceptedSampleCount() == 1,
                 "trusted map accepts in-range band sample");
    test.require(accumulator.rejectedSampleCount() == 1,
                 "trusted map rejects out-of-range band sample");
}

void testTrustedStreamingRejectsOutOfOrderSamplePerBand(TestRunner& test)
{
    SignalParameterAccumulator accumulator(trustedStreamingVectorMicrosecondSampleConfig());
    accumulator.ingest({makeSample(10), makeSample(12), makeSample(11)});

    test.require(accumulator.acceptedSampleCount() == 2,
                 "trusted streaming accepts only monotonic per-band samples");
    test.require(accumulator.rejectedSampleCount() == 1,
                 "trusted streaming rejects out-of-order per-band sample");
}

void testTrustedStreamingVectorHandlesLargeMonotonicBlock(TestRunner& test)
{
    constexpr std::uint64_t sampleCount = 50'000;
    constexpr int bandCount = 4;

    std::vector<SignalSample> samples;
    samples.reserve(sampleCount);
    for (std::uint64_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const auto bandIndex = static_cast<int>(sampleIndex % bandCount);
        samples.push_back(makeSample(sampleIndex,
                                     bandIndex,
                                     1'000'000'000LL
                                         + static_cast<std::int64_t>(bandIndex)
                                             * 100'000'000LL));
    }

    SignalParameterAccumulator accumulator(
        trustedStreamingVectorMicrosecondSampleConfig(bandCount));
    accumulator.ingest(samples);

    test.require(accumulator.acceptedSampleCount() == sampleCount,
                 "trusted vector large block accepts all monotonic samples");
    test.require(accumulator.rejectedSampleCount() == 0,
                 "trusted vector large block rejects no monotonic samples");
    test.require(!accumulator.finalize().empty(),
                 "trusted vector large block produces signal parameters");
}

void testSortedAcceptsOutOfOrderInput(TestRunner& test)
{
    SignalParameterAccumulator accumulator(microsecondSampleConfig());
    accumulator.ingest({makeSample(10), makeSample(12), makeSample(11)});

    const auto result = accumulator.finalize();

    test.require(accumulator.acceptedSampleCount() == 3,
                 "sorted mode accepts out-of-order input after sorting");
    test.require(accumulator.rejectedSampleCount() == 0,
                 "sorted mode does not reject sortable out-of-order input");
    test.require(result.size() == 1, "sorted out-of-order input produces one estimate");
    if (!result.empty()) {
        test.require(result.front().pulseCount == 1,
                     "sorted out-of-order input stays in one pulse");
        test.require(nearly(result.front().pulseWidthUs, 3.0),
                     "sorted out-of-order input calculates PW after sorting");
    }
}

void testDuplicateSampleIndexWithDifferentBeamStaysInsideOnePulse(TestRunner& test)
{
    SignalParameterAccumulator accumulator(microsecondSampleConfig());
    accumulator.ingest({makeSample(10, 0, 1'000'000'000LL, 32, 0),
                        makeSample(10, 0, 1'000'000'000LL, 31, 1),
                        makeSample(11, 0, 1'000'000'000LL, 32, 0),
                        makeSample(11, 0, 1'000'000'000LL, 31, 1)});

    const auto result = accumulator.finalize();

    test.require(result.size() == 1, "duplicate beam samples produce one band estimate");
    test.require(accumulator.acceptedSampleCount() == 4,
                 "duplicate beam samples are accepted");
    if (result.empty()) {
        return;
    }

    test.require(result.front().pulseCount == 1,
                 "duplicate sample indexes stay in one pulse");
    test.require(nearly(result.front().pulseWidthUs, 2.0),
                 "duplicate sample indexes do not inflate PW");
}

void testInvalidSamplesAreRejected(TestRunner& test)
{
    SignalParameterAccumulator accumulator(microsecondSampleConfig());
    accumulator.ingest({makeSample(1, 0, 1'000'000'000LL, 0),
                        makeSample(2, 99),
                        makeSample(3, 0, 1'000'000'000LL, 32, 9),
                        makeSample(4, 0, 1),
                        makeSample(10),
                        makeSample(11)});

    const auto result = accumulator.finalize();

    test.require(accumulator.acceptedSampleCount() == 2,
                 "only valid samples are accepted");
    test.require(accumulator.rejectedSampleCount() == 4,
                 "invalid samples are rejected");
    test.require(result.size() == 1, "valid tail samples still produce parameters");
    if (!result.empty()) {
        test.require(result.front().pulseCount == 1,
                     "invalid samples do not create pulses");
        test.require(nearly(result.front().pulseWidthUs, 2.0),
                     "invalid samples do not affect PW");
    }
}

void testUniqueFrequencyConfigIsAppliedOnFinalize(TestRunner& test)
{
    SignalParameterEstimatorConfig uniqueConfig = microsecondSampleConfig();
    uniqueConfig.uniqueFrequencies = true;
    SignalParameterAccumulator uniqueAccumulator(uniqueConfig);
    uniqueAccumulator.ingest({makeSample(20, 0, 1'300'000'000LL),
                              makeSample(10, 0, 1'100'000'000LL),
                              makeSample(30, 0, 1'100'000'000LL)});

    SignalParameterEstimatorConfig orderedConfig = microsecondSampleConfig();
    orderedConfig.uniqueFrequencies = false;
    SignalParameterAccumulator orderedAccumulator(orderedConfig);
    orderedAccumulator.ingest({makeSample(20, 0, 1'300'000'000LL),
                               makeSample(10, 0, 1'100'000'000LL),
                               makeSample(30, 0, 1'100'000'000LL)});

    const auto uniqueResult = uniqueAccumulator.finalize();
    const auto orderedResult = orderedAccumulator.finalize();

    test.require(uniqueResult.size() == 1, "unique frequency result exists");
    test.require(orderedResult.size() == 1, "ordered frequency result exists");
    if (!uniqueResult.empty()) {
        test.require(uniqueResult.front().frequenciesHz
                         == std::vector<std::int64_t>{1'100'000'000LL, 1'300'000'000LL},
                     "unique frequencies are sorted and deduplicated");
    }
    if (!orderedResult.empty()) {
        test.require(orderedResult.front().frequenciesHz
                         == std::vector<std::int64_t>{
                             1'100'000'000LL,
                             1'300'000'000LL,
                             1'100'000'000LL,
                         },
                     "non-unique frequencies preserve pulse order");
    }
}

} // namespace

int main()
{
    TestRunner test;

    testEmptyAccumulatorReturnsNoParameters(test);
    testOnePulseGivesPulseWidthWithoutPri(test);
    testTwoPulsesGiveAveragePulseWidthAndPri(test);
    testStreamingBatchesMatchOneBatch(test);
    testBandsAreCalculatedIndependently(test);
    testDuplicateSampleIndexWithDifferentBeamStaysInsideOnePulse(test);
    testStreamingEqualsSortedForMonotonicPerBandInput(test);
    testStreamingRejectsOutOfOrderSamplePerBand(test);
    testTrustedStreamingVectorMatchesValidatedStreaming(test);
    testTrustedVectorRejectsOutOfCapacityBand(test);
    testTrustedMapRejectsOutOfRangeBand(test);
    testTrustedStreamingRejectsOutOfOrderSamplePerBand(test);
    testTrustedStreamingVectorHandlesLargeMonotonicBlock(test);
    testSortedAcceptsOutOfOrderInput(test);
    testInvalidSamplesAreRejected(test);
    testUniqueFrequencyConfigIsAppliedOnFinalize(test);

    return test.result();
}
