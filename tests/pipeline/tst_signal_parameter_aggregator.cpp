#include "pipeline/signal_parameter_aggregator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace siriusscope;

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

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

pipeline::SignalParameterAggregatorConfig microsecondConfig()
{
    pipeline::SignalParameterAggregatorConfig config;
    config.estimatorConfig.samplePeriodNs = 1000;
    return config;
}

core::SignalSample sample(std::uint64_t sampleIndex,
                          int bandIndex = 0,
                          std::int64_t frequencyHz = 3'000'000'000LL)
{
    return core::SignalSample{
        sampleIndex,
        bandIndex,
        0,
        frequencyHz,
        80,
        0,
    };
}

const pipeline::BandSignalParametersSummary* findBand(
    const pipeline::SignalParameterSnapshot& snapshot,
    int bandIndex)
{
    const auto found =
        std::find_if(snapshot.bands.cbegin(), snapshot.bands.cend(), [bandIndex](const auto& item) {
            return item.bandIndex == bandIndex;
        });
    return found == snapshot.bands.cend() ? nullptr : &*found;
}

bool nearly(double actual, double expected, double tolerance = 0.001)
{
    return std::abs(actual - expected) <= tolerance;
}

void testEmptyAggregatorReturnsEmptySnapshot(TestRunner& test)
{
    pipeline::SignalParameterAggregator aggregator;
    const auto snapshot = aggregator.makeSnapshot();

    test.require(snapshot != nullptr, "empty signal parameter snapshot is created");
    test.require(snapshot && snapshot->bands.empty(),
                 "empty signal parameter snapshot has no bands");
    test.require(snapshot && snapshot->acceptedSampleCount == 0,
                 "empty signal parameter snapshot has zero accepted samples");
}

void testOnePulseGivesPulseWidthWithoutPri(TestRunner& test)
{
    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    const std::vector<core::SignalSample> samples{
        sample(10),
        sample(11),
        sample(12),
    };
    const auto result = aggregator.consume(samples);
    const auto snapshot = result.snapshot;
    const auto* band0 = snapshot ? findBand(*snapshot, 0) : nullptr;

    test.require(snapshot != nullptr, "one-pulse consume publishes signal parameter snapshot");
    test.require(band0 != nullptr, "one-pulse snapshot contains band 0");
    if (!band0) {
        return;
    }

    test.require(band0->pulseCount == 1, "one-pulse snapshot counts one pulse");
    test.require(band0->pulseWidthUs && nearly(*band0->pulseWidthUs, 3.0),
                 "one-pulse snapshot calculates PW");
    test.require(!band0->pulseRepetitionPeriodUs,
                 "one-pulse snapshot has no PRI");
    test.require(band0->firstSampleIndex == 10 && band0->lastSampleIndex == 12,
                 "one-pulse snapshot tracks compact sample span");
}

void testTwoPulsesGivePriAndAveragePulseWidth(TestRunner& test)
{
    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    const std::vector<core::SignalSample> samples{
        sample(10),
        sample(11),
        sample(20),
        sample(21),
        sample(22),
    };
    const auto result = aggregator.consume(samples);
    const auto snapshot = result.snapshot;
    const auto* band0 = snapshot ? findBand(*snapshot, 0) : nullptr;

    test.require(band0 != nullptr, "two-pulse snapshot contains band 0");
    if (!band0) {
        return;
    }

    test.require(band0->pulseCount == 2, "two-pulse snapshot counts pulses");
    test.require(band0->pulseWidthUs && nearly(*band0->pulseWidthUs, 2.5),
                 "two-pulse snapshot calculates average PW");
    test.require(band0->pulseRepetitionPeriodUs
                     && nearly(*band0->pulseRepetitionPeriodUs, 10.0),
                 "two-pulse snapshot calculates PRI");
    test.require(snapshot->acceptedSampleCount == 5,
                 "two-pulse snapshot counts accepted samples");
    test.require(snapshot->pulseCount == 2,
                 "two-pulse snapshot exposes total pulse count");
}

void testBandsCalculatedIndependently(TestRunner& test)
{
    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    const std::vector<core::SignalSample> samples{
        sample(10, 0, 3'000'000'000LL),
        sample(11, 0, 3'000'000'000LL),
        sample(20, 1, 4'000'000'000LL),
        sample(21, 1, 4'000'000'000LL),
        sample(30, 1, 4'000'000'000LL),
        sample(31, 1, 4'000'000'000LL),
    };
    const auto result = aggregator.consume(samples);
    const auto snapshot = result.snapshot;

    test.require(snapshot != nullptr, "multi-band consume publishes snapshot");
    test.require(snapshot && snapshot->bands.size() == 2,
                 "signal parameters are summarized independently per band");
    test.require(snapshot && findBand(*snapshot, 0) != nullptr,
                 "multi-band snapshot contains band 0");
    test.require(snapshot && findBand(*snapshot, 1) != nullptr,
                 "multi-band snapshot contains band 1");
}

void testDefaultAggregatorUsesStreamingIngest(TestRunner& test)
{
    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    const std::vector<core::SignalSample> samples{
        sample(10),
        sample(12),
        sample(11),
    };

    const auto result = aggregator.consume(samples);
    const auto snapshot = result.snapshot;

    test.require(snapshot != nullptr, "streaming default publishes snapshot");
    test.require(result.acceptedSampleDelta == 2,
                 "streaming default accepts monotonic sample delta");
    test.require(result.rejectedSampleDelta == 1,
                 "streaming default rejects out-of-order per-band sample delta");
    test.require(snapshot && snapshot->acceptedSampleCount == 2,
                 "streaming default snapshot counts accepted samples");
    test.require(snapshot && snapshot->rejectedSampleCount == 1,
                 "streaming default snapshot counts rejected samples");
}

void testStreamingIngestHandlesLargeMonotonicBlock(TestRunner& test)
{
    constexpr std::uint64_t sampleCount = 50'000;

    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    std::vector<core::SignalSample> samples;
    samples.reserve(sampleCount);
    for (std::uint64_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        samples.push_back(sample(sampleIndex));
    }

    const auto result = aggregator.consume(samples);
    const auto snapshot = result.snapshot;

    test.require(snapshot != nullptr, "large streaming block publishes snapshot");
    test.require(result.acceptedSampleDelta == sampleCount,
                 "large streaming block accepts all sample deltas");
    test.require(result.rejectedSampleDelta == 0,
                 "large streaming block rejects no monotonic samples");
    test.require(snapshot && snapshot->acceptedSampleCount == sampleCount,
                 "large streaming block snapshot counts accepted samples");
}

} // namespace

int main()
{
    TestRunner test;

    testEmptyAggregatorReturnsEmptySnapshot(test);
    testOnePulseGivesPulseWidthWithoutPri(test);
    testTwoPulsesGivePriAndAveragePulseWidth(test);
    testBandsCalculatedIndependently(test);
    testDefaultAggregatorUsesStreamingIngest(test);
    testStreamingIngestHandlesLargeMonotonicBlock(test);

    return test.result();
}
