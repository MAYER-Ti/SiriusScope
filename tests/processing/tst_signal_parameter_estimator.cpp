#include "processing/signal_parameter_estimator.h"

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

const SignalParameters* findParameters(const std::vector<SignalParameters>& estimates,
                                       int bandIndex)
{
    const auto found = std::find_if(estimates.begin(), estimates.end(), [bandIndex](const auto& item) {
        return item.bandIndex == bandIndex;
    });
    return found == estimates.end() ? nullptr : &(*found);
}

void testEmptyInput(TestRunner& test)
{
    const SignalParameterEstimator estimator;

    test.require(estimator.buildPulses({}).empty(), "empty input builds no pulses");
    test.require(estimator.estimate({}).empty(), "empty input estimates no parameters");
}

void testSinglePulseWidthWithoutPri(TestRunner& test)
{
    SignalParameterEstimatorConfig config;
    config.samplePeriodNs = 1000;
    SignalParameterEstimator estimator(config);

    const auto estimates = estimator.estimate({makeSample(10), makeSample(11), makeSample(12)});

    test.require(estimates.size() == 1, "single pulse produces one band estimate");
    if (estimates.empty()) {
        return;
    }

    test.require(estimates.front().pulseCount == 1, "single pulse count is preserved");
    test.require(nearly(estimates.front().pulseWidthUs, 3.0), "single pulse width is calculated");
    test.require(!estimates.front().pulseRepetitionPeriodUs, "single pulse has no PRI");
}

void testTwoPulsesWidthAndPri(TestRunner& test)
{
    SignalParameterEstimatorConfig config;
    config.samplePeriodNs = 1000;
    SignalParameterEstimator estimator(config);

    const auto estimates =
        estimator.estimate({makeSample(10), makeSample(11), makeSample(20), makeSample(21), makeSample(22)});

    test.require(estimates.size() == 1, "two pulses produce one band estimate");
    if (estimates.empty()) {
        return;
    }

    test.require(estimates.front().pulseCount == 2, "two pulses are counted");
    test.require(nearly(estimates.front().pulseWidthUs, 2.5), "average pulse width is calculated");
    test.require(estimates.front().pulseRepetitionPeriodUs
                     && nearly(*estimates.front().pulseRepetitionPeriodUs, 10.0),
                 "average PRI is calculated");
}

void testBandsCalculatedIndependently(TestRunner& test)
{
    SignalParameterEstimatorConfig config;
    config.samplePeriodNs = 1000;
    SignalParameterEstimator estimator(config);

    const auto estimates =
        estimator.estimate({makeSample(10, 0), makeSample(11, 0), makeSample(30, 1), makeSample(32, 1)});

    const auto* band0 = findParameters(estimates, 0);
    const auto* band1 = findParameters(estimates, 1);

    test.require(estimates.size() == 2, "two valid bands produce two estimates");
    test.require(band0 != nullptr && nearly(band0->pulseWidthUs, 2.0), "band 0 width is independent");
    test.require(band1 != nullptr && nearly(band1->pulseWidthUs, 1.0), "band 1 width is independent");
}

void testThresholdGapMergesPulse(TestRunner& test)
{
    SignalParameterEstimatorConfig config;
    config.maxIntraPulseGapSamples = 2;
    SignalParameterEstimator estimator(config);

    const auto pulses = estimator.buildPulses({makeSample(100), makeSample(102), makeSample(104)});

    test.require(pulses.size() == 1, "gap equal to threshold keeps one pulse");
    test.require(!pulses.empty() && pulses.front().firstSampleIndex == 100
                     && pulses.front().lastSampleIndex == 104 && pulses.front().sampleCount == 3,
                 "merged pulse preserves span and count");
}

void testGapAboveThresholdStartsNewPulse(TestRunner& test)
{
    SignalParameterEstimatorConfig config;
    config.maxIntraPulseGapSamples = 2;
    SignalParameterEstimator estimator(config);

    const auto pulses = estimator.buildPulses({makeSample(100), makeSample(103)});

    test.require(pulses.size() == 2, "gap above threshold starts a new pulse");
}

void testInvalidAmplitudeIgnored(TestRunner& test)
{
    SignalParameterEstimator estimator;

    const auto pulses = estimator.buildPulses({makeSample(10, 0, 1'000'000'000LL, 0),
                                               makeSample(11, 0, 1'000'000'000LL, 128),
                                               makeSample(12, 0, 1'000'000'000LL, 42)});

    test.require(pulses.size() == 1, "only valid amplitude participates");
    test.require(!pulses.empty() && pulses.front().peakAmplitude == 42,
                 "valid amplitude is preserved after invalid samples");
}

void testRepresentativeFrequencyUsesPeakAmplitude(TestRunner& test)
{
    SignalParameterEstimator estimator;

    const auto pulses = estimator.buildPulses({makeSample(10, 0, 1'100'000'000LL, 20),
                                               makeSample(11, 0, 1'200'000'000LL, 80),
                                               makeSample(12, 0, 1'300'000'000LL, 60)});

    test.require(pulses.size() == 1, "peak frequency test builds one pulse");
    test.require(!pulses.empty() && pulses.front().representativeFrequencyHz == 1'200'000'000LL,
                 "representative frequency comes from peak amplitude sample");
}

void testFrequenciesSortedAndUnique(TestRunner& test)
{
    SignalParameterEstimatorConfig config;
    config.uniqueFrequencies = true;
    SignalParameterEstimator estimator(config);

    const auto estimates = estimator.estimate({makeSample(30, 0, 1'300'000'000LL, 50),
                                               makeSample(10, 0, 1'100'000'000LL, 50),
                                               makeSample(20, 0, 1'100'000'000LL, 50)});

    test.require(estimates.size() == 1, "frequency estimate is produced");
    test.require(!estimates.empty()
                     && estimates.front().frequenciesHz
                         == std::vector<std::int64_t>{1'100'000'000LL, 1'300'000'000LL},
                 "frequencies are sorted and unique");
}

void testConfigIsNormalized(TestRunner& test)
{
    SignalParameterEstimatorConfig config;
    config.samplePeriodNs = 0;
    config.maxIntraPulseGapSamples = 0;
    config.minSamplesPerPulse = 0;
    SignalParameterEstimator estimator(config);

    const auto estimates = estimator.estimate({makeSample(10), makeSample(11)});

    test.require(estimates.size() == 1, "normalized config produces an estimate");
    test.require(!estimates.empty() && estimates.front().pulseCount == 1,
                 "zero gap threshold is normalized to one sample");
    test.require(!estimates.empty() && nearly(estimates.front().pulseWidthUs, 0.002),
                 "zero sample period is normalized to one nanosecond");
}

} // namespace

int main()
{
    TestRunner test;

    testEmptyInput(test);
    testSinglePulseWidthWithoutPri(test);
    testTwoPulsesWidthAndPri(test);
    testBandsCalculatedIndependently(test);
    testThresholdGapMergesPulse(test);
    testGapAboveThresholdStartsNewPulse(test);
    testInvalidAmplitudeIgnored(test);
    testRepresentativeFrequencyUsesPeakAmplitude(test);
    testFrequenciesSortedAndUnique(test);
    testConfigIsNormalized(test);

    return test.result();
}
