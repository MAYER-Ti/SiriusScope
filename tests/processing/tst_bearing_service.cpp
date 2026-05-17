#include "processing/bearing_service.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
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

bool nearly(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

bool nearAzimuth(double actual, double expected, double tolerance)
{
    const auto diff = std::abs(actual - expected);
    return std::min(diff, 360.0 - diff) <= tolerance;
}

bool hasDiagnostic(const BearingCalculationResult& result, ProcessingErrorCode code)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [code](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

TimeBase makeTimeBase(std::int64_t startUtcNs = 1'000'000,
                      std::uint64_t firstSampleIndex = 0,
                      std::uint64_t samplePeriodNs = DomainConstraints::defaultSamplePeriodNs)
{
    auto created = TimeBase::create(startUtcNs, firstSampleIndex, samplePeriodNs);
    return *created.value();
}

BearingCandidate makeCandidate(int bandIndex,
                               std::uint64_t sampleIndex,
                               int leftAmplitude,
                               int rightAmplitude,
                               std::int64_t minHz = 1'000'000'000LL,
                               std::int64_t maxHz = 1'001'000'000LL)
{
    BearingCandidate candidate;
    candidate.bandIndex = bandIndex;
    candidate.sampleIndexStart = sampleIndex;
    candidate.sampleIndexEnd = sampleIndex;
    candidate.frequencyBin = 0;
    candidate.frequencyRange = FrequencyRange{minHz, maxHz};
    candidate.beamAmplitudes = {leftAmplitude, rightAmplitude};
    candidate.beamPresent = {true, true};
    return candidate;
}

BearingFrameObservation makeObservation(BearingCandidate candidate,
                                        double antennaAzimuthDeg,
                                        std::int64_t observedUtcNs = 10'000'000)
{
    BearingInputFrame frame;
    frame.bandIndex = candidate.bandIndex;
    frame.sampleIndexStart = candidate.sampleIndexStart;
    frame.sampleIndexEnd = candidate.sampleIndexEnd;
    frame.candidates.push_back(std::move(candidate));
    return BearingFrameObservation{std::move(frame), antennaAzimuthDeg, observedUtcNs};
}

void testEmptyInput(TestRunner& test)
{
    const auto result = BearingService{}.calculate({}, makeTimeBase());

    test.require(result.results.empty(), "empty input has no results");
    test.require(hasDiagnostic(result, ProcessingErrorCode::BearingNoObservations),
                 "empty input reports no observations");
}

void testSingleBandTwoBeams(TestRunner& test)
{
    const auto result = BearingService{}.calculate(
        {makeObservation(makeCandidate(0, 20, 100, 80), 50.0)},
        makeTimeBase(1'000'000, 10, 320));

    test.require(result.results.size() == 1, "one complete candidate produces one result");
    if (result.results.empty()) {
        return;
    }

    test.require(result.results.front().bandIndex == 0, "result preserves bandIndex");
    test.require(nearly(result.results.front().bearingAzimuthDeg, 46.6667, 0.2),
                 "left beam shifts bearing left");
    test.require(!result.results.front().frequenciesHz.empty(),
                 "result carries center frequency");
    test.require(result.results.front().quality && *result.results.front().quality > 0.0,
                 "result carries positive quality");
}

void testRightBeamStronger(TestRunner& test)
{
    const auto result = BearingService{}.calculate(
        {makeObservation(makeCandidate(0, 20, 70, 100), 50.0)},
        makeTimeBase());

    test.require(result.results.size() == 1, "right beam candidate produces a result");
    test.require(!result.results.empty() && result.results.front().bearingAzimuthDeg > 50.0,
                 "right beam shifts bearing right");
}

void testSeparateBands(TestRunner& test)
{
    const std::vector<BearingFrameObservation> observations{
        makeObservation(makeCandidate(0, 10, 100, 80), 20.0),
        makeObservation(makeCandidate(2, 11, 90, 70, 2'000'000'000LL, 2'001'000'000LL), 30.0),
        makeObservation(makeCandidate(4, 12, 80, 60, 3'000'000'000LL, 3'001'000'000LL), 40.0),
    };

    const auto result = BearingService{}.calculate(observations, makeTimeBase());

    test.require(result.results.size() == 3, "results are calculated per BandItem");
    test.require(std::any_of(result.results.begin(), result.results.end(), [](const auto& item) {
                     return item.bandIndex == 0;
                 }),
                 "result contains band 0");
    test.require(std::any_of(result.results.begin(), result.results.end(), [](const auto& item) {
                     return item.bandIndex == 2;
                 }),
                 "result contains band 2");
    test.require(std::any_of(result.results.begin(), result.results.end(), [](const auto& item) {
                     return item.bandIndex == 4;
                 }),
                 "result contains band 4");
}

void testMissingBeam(TestRunner& test)
{
    auto candidate = makeCandidate(0, 20, 70, 0);
    candidate.beamPresent[1] = false;

    const auto result = BearingService{}.calculate(
        {makeObservation(std::move(candidate), 50.0)},
        makeTimeBase());

    test.require(result.results.empty(), "missing beam candidate is skipped");
    test.require(hasDiagnostic(result, ProcessingErrorCode::MissingBeamSample),
                 "missing beam is diagnosed");
    test.require(hasDiagnostic(result, ProcessingErrorCode::BearingNoCandidates),
                 "missing beam leaves no usable candidates");
}

void testNormalizeAngle(TestRunner& test)
{
    const auto result = BearingService{}.calculate(
        {makeObservation(makeCandidate(0, 20, 127, 1), 1.0)},
        makeTimeBase());

    test.require(result.results.size() == 1, "wrapped bearing produces a result");
    test.require(!result.results.empty() && result.results.front().bearingAzimuthDeg > 330.0
                     && result.results.front().bearingAzimuthDeg < 360.0,
                 "bearing is normalized to 0..360");
}

void testCircularMeanAcrossZero(TestRunner& test)
{
    const std::vector<BearingFrameObservation> observations{
        makeObservation(makeCandidate(0, 20, 80, 80), 359.0),
        makeObservation(makeCandidate(0, 21, 80, 80), 1.0),
    };

    const auto result = BearingService{}.calculate(observations, makeTimeBase());

    test.require(result.results.size() == 1, "two observations produce one result");
    test.require(!result.results.empty()
                     && nearAzimuth(result.results.front().bearingAzimuthDeg, 0.0, 1.0),
                 "circular mean crosses zero correctly");
}

void testTimeBaseConversion(TestRunner& test)
{
    const auto result = BearingService{}.calculate(
        {makeObservation(makeCandidate(0, 20, 100, 80), 50.0)},
        makeTimeBase(1'000'000, 10, 320));

    test.require(result.results.size() == 1, "timebase test produces a result");
    test.require(!result.results.empty() && result.results.front().resultTimeUtcNs == 1'003'200,
                 "result UTC time is derived from sampleIndex");
}

void testTimeBaseFallback(TestRunner& test)
{
    const auto result = BearingService{}.calculate(
        {makeObservation(makeCandidate(0, 20, 100, 80), 50.0, 12'345)},
        makeTimeBase(1'000'000, 100, 320));

    test.require(result.results.size() == 1, "fallback time test produces a result");
    test.require(!result.results.empty() && result.results.front().resultTimeUtcNs == 12'345,
                 "invalid timebase mapping falls back to observed UTC");
    test.require(hasDiagnostic(result, ProcessingErrorCode::BearingTimeConversionFailed),
                 "timebase fallback is diagnosed");
}

void testDomainValidationFailure(TestRunner& test)
{
    const auto result = BearingService{}.calculate(
        {makeObservation(makeCandidate(9, 20, 100, 80), 50.0)},
        makeTimeBase());

    test.require(result.results.empty(), "invalid band result is rejected");
    test.require(hasDiagnostic(result, ProcessingErrorCode::BearingResultRejected),
                 "domain validation failure is diagnosed");
}

void testLowQualityRejection(TestRunner& test)
{
    BearingServiceConfig config;
    config.minResultQuality = 0.9;
    const auto result = BearingService{config}.calculate(
        {makeObservation(makeCandidate(0, 20, 2, 1), 50.0)},
        makeTimeBase());

    test.require(result.results.empty(), "low quality result is rejected");
    test.require(hasDiagnostic(result, ProcessingErrorCode::BearingQualityBelowThreshold),
                 "low quality is diagnosed");
}

} // namespace

int main()
{
    TestRunner test;

    testEmptyInput(test);
    testSingleBandTwoBeams(test);
    testRightBeamStronger(test);
    testSeparateBands(test);
    testMissingBeam(test);
    testNormalizeAngle(test);
    testCircularMeanAcrossZero(test);
    testTimeBaseConversion(test);
    testTimeBaseFallback(test);
    testDomainValidationFailure(test);
    testLowQualityRejection(test);

    return test.result();
}
