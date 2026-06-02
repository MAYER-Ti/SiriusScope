#include "pipeline/spectrum_aggregator.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
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

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

pipeline::SpectrumAggregatorConfig testConfig()
{
    pipeline::SpectrumAggregatorConfig config;
    config.renderBinCount = 11;
    config.sourceMinHz = 100;
    config.sourceMaxHz = 200;
    config.snapshotPeriodNs = 20'000'000;
    config.amplitudeFloor = 0;
    config.separateBeams = true;
    config.timeBase = core::TimeBase{0, 0, 1'000'000};
    return config;
}

core::SignalSample makeSample(std::uint64_t sampleIndex,
                              std::int64_t frequencyHz,
                              int amplitude,
                              int beamIndex,
                              int bandIndex = 0)
{
    return core::SignalSample{
        sampleIndex,
        bandIndex,
        0,
        frequencyHz,
        amplitude,
        beamIndex,
    };
}

std::shared_ptr<const pipeline::SpectrumSnapshot> consumeAndFlush(
    pipeline::SpectrumAggregator& aggregator,
    const std::vector<core::SignalSample>& samples)
{
    aggregator.consume(samples);
    const auto flushed = aggregator.flush();
    return flushed.snapshots.empty() ? nullptr : flushed.snapshots.front();
}

std::vector<std::shared_ptr<const pipeline::SpectrumSnapshot>> consumeAndFlushAll(
    pipeline::SpectrumAggregator& aggregator,
    const std::vector<core::SignalSample>& samples)
{
    auto consumed = aggregator.consume(samples);
    auto flushed = aggregator.flush();
    consumed.snapshots.insert(consumed.snapshots.end(),
                              flushed.snapshots.begin(),
                              flushed.snapshots.end());
    return consumed.snapshots;
}

pipeline::SpectrumAggregatorConfig legacyConfig()
{
    auto config = testConfig();
    config.windowIndexMode = pipeline::SpectrumWindowIndexMode::ExactInt128;
    config.useFastWindowIndex = false;
    config.useFastBinIndex = false;
    config.useFixedBandSummaryStorage = false;
    return config;
}

pipeline::SpectrumAggregatorConfig fastConfig(std::size_t bandCapacity = 4)
{
    auto config = testConfig();
    config.useFastWindowIndex = true;
    config.useFastBinIndex = true;
    config.useFixedBandSummaryStorage = true;
    config.bandCapacity = bandCapacity;
    return config;
}

pipeline::SpectrumAggregatorConfig divisibleWindowConfig()
{
    auto config = fastConfig();
    config.windowIndexMode = pipeline::SpectrumWindowIndexMode::DivisibleSamplePeriod;
    return config;
}

pipeline::SpectrumAggregatorConfig nonDivisibleIncrementalConfig()
{
    auto config = fastConfig();
    config.windowIndexMode =
        pipeline::SpectrumWindowIndexMode::IncrementalMonotonic;
    config.snapshotPeriodNs = 20'000'000;
    config.timeBase = core::TimeBase{0, 0, 7'000'000};
    return config;
}

std::uint64_t expectedFirstSampleIndexForWindow(
    const pipeline::SpectrumAggregatorConfig& config,
    std::uint64_t windowIndex)
{
    const auto numerator =
        static_cast<unsigned __int128>(windowIndex)
        * static_cast<unsigned __int128>(config.snapshotPeriodNs);
    const auto denominator =
        static_cast<unsigned __int128>(config.timeBase.samplePeriodNs);
    auto relative = numerator / denominator;
    if (numerator % denominator != 0) {
        ++relative;
    }
    return config.timeBase.firstSampleIndex
        + static_cast<std::uint64_t>(relative);
}

const pipeline::SpectrumBandSummary* summaryFor(
    const std::vector<pipeline::SpectrumBandSummary>& summaries,
    int bandIndex)
{
    const auto found = std::find_if(summaries.begin(),
                                    summaries.end(),
                                    [bandIndex](const auto& summary) {
                                        return summary.bandIndex == bandIndex;
                                    });
    return found == summaries.end() ? nullptr : &*found;
}

void requireSameBins(TestRunner& test,
                     const pipeline::SpectrumSnapshot& left,
                     const pipeline::SpectrumSnapshot& right,
                     const std::string& message)
{
    test.require(left.bins.size() == right.bins.size(),
                 message + ": bin count matches");
    const auto count = std::min(left.bins.size(), right.bins.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& leftBin = left.bins[index];
        const auto& rightBin = right.bins[index];
        test.require(leftBin.totalPeak == rightBin.totalPeak
                         && leftBin.beam0Peak == rightBin.beam0Peak
                         && leftBin.beam1Peak == rightBin.beam1Peak
                         && leftBin.hitCount == rightBin.hitCount,
                     message + ": bin " + std::to_string(index) + " matches");
    }
}

void requireSameBandSummaries(TestRunner& test,
                              const pipeline::SpectrumSnapshot& left,
                              const pipeline::SpectrumSnapshot& right,
                              const std::string& message)
{
    test.require(left.bandSummaries.size() == right.bandSummaries.size(),
                 message + ": band summary count matches");
    for (const auto& leftSummary : left.bandSummaries) {
        const auto* rightSummary = summaryFor(right.bandSummaries,
                                              leftSummary.bandIndex);
        test.require(rightSummary != nullptr,
                     message + ": matching band summary exists");
        if (!rightSummary) {
            continue;
        }
        test.require(leftSummary.sampleCount == rightSummary->sampleCount
                         && leftSummary.totalPeak == rightSummary->totalPeak
                         && leftSummary.beam0Peak == rightSummary->beam0Peak
                         && leftSummary.beam1Peak == rightSummary->beam1Peak,
                     message + ": band " + std::to_string(leftSummary.bandIndex)
                         + " summary matches");
    }
}

void requireSameSnapshots(TestRunner& test,
                          const std::vector<std::shared_ptr<const pipeline::SpectrumSnapshot>>& left,
                          const std::vector<std::shared_ptr<const pipeline::SpectrumSnapshot>>& right,
                          const std::string& message)
{
    test.require(left.size() == right.size(),
                 message + ": snapshot count matches");
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        test.require(left[index] != nullptr && right[index] != nullptr,
                     message + ": snapshot " + std::to_string(index)
                         + " exists");
        if (!left[index] || !right[index]) {
            continue;
        }
        test.require(left[index]->firstSampleIndex == right[index]->firstSampleIndex
                         && left[index]->lastSampleIndex
                             == right[index]->lastSampleIndex,
                     message + ": snapshot " + std::to_string(index)
                         + " sample range matches");
        requireSameBins(test,
                        *left[index],
                        *right[index],
                        message + ": snapshot " + std::to_string(index));
        requireSameBandSummaries(test,
                                 *left[index],
                                 *right[index],
                                 message + ": snapshot "
                                     + std::to_string(index));
    }
}

void testFrequencyMapsToExpectedBin(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(testConfig());
    const auto snapshot =
        consumeAndFlush(aggregator, {makeSample(0, 150, 70, 0)});

    test.require(snapshot != nullptr, "flush publishes spectrum snapshot");
    test.require(snapshot->bins.size() == 11, "snapshot bin count matches render config");
    test.require(snapshot->bins[5].totalPeak == 70,
                 "frequency maps to expected spectrum bin");
}

void testBeamPeaksAndTotalPeak(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(testConfig());
    const auto snapshot =
        consumeAndFlush(aggregator,
                        {
                            makeSample(0, 150, 40, 0),
                            makeSample(1, 150, 90, 1),
                            makeSample(2, 150, 60, 0),
                        });

    test.require(snapshot != nullptr, "beam peak test publishes snapshot");
    const auto& bin = snapshot->bins[5];
    test.require(bin.beam0Peak == 60, "beam 0 peak is aggregated separately");
    test.require(bin.beam1Peak == 90, "beam 1 peak is aggregated separately");
    test.require(bin.totalPeak == 90, "total peak is max across beams");
    test.require(bin.hitCount == 3, "hit count aggregates all samples in bin");
}

void testBandSummaries(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(testConfig());
    const auto snapshot =
        consumeAndFlush(aggregator,
                        {
                            makeSample(0, 150, 40, 0, 0),
                            makeSample(1, 150, 90, 1, 1),
                            makeSample(2, 160, 50, 1, 1),
                        });

    test.require(snapshot != nullptr, "band summary test publishes snapshot");
    test.require(snapshot->bandSummaries.size() == 2,
                 "snapshot contains compact per-band summaries");

    bool sawBand0 = false;
    bool sawBand1 = false;
    for (const auto& summary : snapshot->bandSummaries) {
        if (summary.bandIndex == 0) {
            sawBand0 = summary.sampleCount == 1 && summary.beam0Peak == 40
                && summary.totalPeak == 40;
        }
        if (summary.bandIndex == 1) {
            sawBand1 = summary.sampleCount == 2 && summary.beam1Peak == 90
                && summary.totalPeak == 90;
        }
    }

    test.require(sawBand0, "band 0 summary counts samples and peaks");
    test.require(sawBand1, "band 1 summary counts samples and peaks");
}

void testInvalidAndOutOfRangeSamplesAreCounted(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(testConfig());
    const auto snapshot =
        consumeAndFlush(aggregator,
                        {
                            makeSample(0, 150, 70, 0),
                            makeSample(1, 0, 70, 0),
                            makeSample(2, 250, 70, 0),
                            makeSample(3, 150, 0, 0),
                        });

    test.require(snapshot != nullptr, "valid sample still produces snapshot");
    test.require(snapshot->bins[5].hitCount == 1,
                 "invalid and out-of-range samples are not binned");
    test.require(snapshot->counters.invalidSamples == 2,
                 "invalid spectrum samples are aggregated in counters");
    test.require(snapshot->counters.outOfRangeSamples == 1,
                 "out-of-range spectrum samples are aggregated in counters");
}

void testSnapshotPeriodPreventsPerBlockSpam(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(testConfig());
    const std::vector<core::SignalSample> samples{
        makeSample(0, 150, 70, 0),
        makeSample(10, 150, 80, 0),
    };
    const auto result = aggregator.consume(samples);

    const auto flushed = aggregator.flush();

    test.require(result.snapshots.empty(),
                 "same time window does not produce per-block snapshots");
    test.require(flushed.snapshots.size() == 1,
                 "flush publishes the open spectrum aggregation window");
    test.require(flushed.snapshots.front()->bins[5].totalPeak == 80,
                 "flush snapshot contains latest peak in the window");
}

void testSequenceIncreasesAcrossWindows(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(testConfig());
    const std::vector<core::SignalSample> samples{
        makeSample(0, 150, 70, 0),
        makeSample(20, 150, 80, 1),
    };
    const auto firstConsume = aggregator.consume(samples);
    const auto flushed = aggregator.flush();

    test.require(firstConsume.snapshots.size() == 1,
                 "window change closes first spectrum snapshot");
    test.require(flushed.snapshots.size() == 1,
                 "flush closes second spectrum snapshot");
    test.require(firstConsume.snapshots.front()->sequenceId == 1,
                 "first spectrum snapshot sequence starts at one");
    test.require(flushed.snapshots.front()->sequenceId == 2,
                 "spectrum snapshot sequence increases");

    static_assert(std::is_const_v<
                  std::remove_reference_t<decltype(*flushed.snapshots.front())>>,
                  "Spectrum snapshots are exposed as immutable shared_ptr<const T>");
}

void testFastAndLegacyModesProduceEquivalentSnapshot(TestRunner& test)
{
    pipeline::SpectrumAggregator legacyAggregator(legacyConfig());
    pipeline::SpectrumAggregator fastAggregator(fastConfig());
    const std::vector<core::SignalSample> samples{
        makeSample(0, 100, 20, 0, 1),
        makeSample(1, 110, 40, 1, 0),
        makeSample(2, 150, 90, 0, 1),
        makeSample(3, 199, 70, 1, 0),
        makeSample(4, 150, 60, 0, 1),
    };

    const auto legacySnapshot = consumeAndFlush(legacyAggregator, samples);
    const auto fastSnapshot = consumeAndFlush(fastAggregator, samples);

    test.require(legacySnapshot && fastSnapshot,
                 "fast and legacy spectrum modes both publish snapshots");
    if (!legacySnapshot || !fastSnapshot) {
        return;
    }

    requireSameBins(test, *legacySnapshot, *fastSnapshot, "fast/legacy equivalence");
    requireSameBandSummaries(test,
                             *legacySnapshot,
                             *fastSnapshot,
                             "fast/legacy equivalence");
}

void testFixedBandSummaryRejectsOutOfCapacityBand(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(fastConfig(1));

    const auto consumed = aggregator.consume(std::vector<core::SignalSample>{
        makeSample(0, 150, 80, 0, 2),
    });
    const auto flushed = aggregator.flush();

    test.require(consumed.snapshots.empty() && flushed.snapshots.empty(),
                 "fixed band summary storage rejects out-of-capacity sample");
    test.require(aggregator.counters().invalidSamples == 1,
                 "fixed band summary storage counts out-of-capacity band as invalid");
}

void testFastWindowBoundariesMatchLegacy(TestRunner& test)
{
    pipeline::SpectrumAggregator legacyAggregator(legacyConfig());
    pipeline::SpectrumAggregator fastAggregator(divisibleWindowConfig());
    const std::vector<core::SignalSample> samples{
        makeSample(0, 150, 40, 0, 0),
        makeSample(19, 150, 50, 1, 0),
        makeSample(20, 150, 60, 0, 0),
    };

    const auto legacySnapshots = consumeAndFlushAll(legacyAggregator, samples);
    auto fastConsumed = fastAggregator.consume(samples);
    const auto fastFlushed = fastAggregator.flush();
    fastConsumed.snapshots.insert(fastConsumed.snapshots.end(),
                                  fastFlushed.snapshots.begin(),
                                  fastFlushed.snapshots.end());
    const auto& fastSnapshots = fastConsumed.snapshots;

    test.require(legacySnapshots.size() == 2 && fastSnapshots.size() == 2,
                 "fast and legacy window modes split at same boundary");
    test.require(fastConsumed.usedFastWindowIndex,
                 "divisible window mode reports old fast window usage");
    if (legacySnapshots.size() != 2 || fastSnapshots.size() != 2) {
        return;
    }

    for (std::size_t index = 0; index < legacySnapshots.size(); ++index) {
        test.require(legacySnapshots[index]->firstSampleIndex
                             == fastSnapshots[index]->firstSampleIndex
                         && legacySnapshots[index]->lastSampleIndex
                             == fastSnapshots[index]->lastSampleIndex,
                     "fast and legacy window sample ranges match");
        requireSameBins(test,
                        *legacySnapshots[index],
                        *fastSnapshots[index],
                        "fast/legacy window boundary");
        requireSameBandSummaries(test,
                                 *legacySnapshots[index],
                                 *fastSnapshots[index],
                                 "fast/legacy window boundary");
    }
}

void testIncrementalWindowMatchesExactWhenPeriodIsNotDivisible(TestRunner& test)
{
    auto exactConfig = nonDivisibleIncrementalConfig();
    exactConfig.windowIndexMode = pipeline::SpectrumWindowIndexMode::ExactInt128;
    pipeline::SpectrumAggregator exactAggregator(exactConfig);
    pipeline::SpectrumAggregator incrementalAggregator(nonDivisibleIncrementalConfig());
    const std::vector<core::SignalSample> samples{
        makeSample(0, 100, 20, 0, 0),
        makeSample(2, 110, 30, 1, 1),
        makeSample(3, 120, 40, 0, 0),
        makeSample(5, 130, 50, 1, 1),
        makeSample(6, 140, 60, 0, 0),
        makeSample(8, 150, 70, 1, 1),
        makeSample(9, 160, 80, 0, 0),
        makeSample(12, 170, 90, 1, 1),
    };

    const auto exactSnapshots = consumeAndFlushAll(exactAggregator, samples);
    auto incrementalConsumed = incrementalAggregator.consume(samples);
    auto incrementalFlushed = incrementalAggregator.flush();
    incrementalConsumed.snapshots.insert(incrementalConsumed.snapshots.end(),
                                         incrementalFlushed.snapshots.begin(),
                                         incrementalFlushed.snapshots.end());

    test.require(incrementalConsumed.usedIncrementalWindowIndex,
                 "incremental non-divisible mode reports incremental usage");
    test.require(incrementalConsumed.incrementalWindowFallbacks == 0,
                 "monotonic non-divisible stream does not need fallback");
    requireSameSnapshots(test,
                         exactSnapshots,
                         incrementalConsumed.snapshots,
                         "incremental/exact non-divisible equivalence");
}

void testIncrementalWindowSplitsAtNonDivisibleBoundaries(TestRunner& test)
{
    const auto config = nonDivisibleIncrementalConfig();
    const auto firstBoundary = expectedFirstSampleIndexForWindow(config, 1);
    const auto secondBoundary = expectedFirstSampleIndexForWindow(config, 2);
    pipeline::SpectrumAggregator aggregator(config);

    const auto snapshots = consumeAndFlushAll(
        aggregator,
        {
            makeSample(0, 150, 20, 0, 0),
            makeSample(firstBoundary - 1, 150, 30, 1, 0),
            makeSample(firstBoundary, 150, 40, 0, 0),
            makeSample(firstBoundary + 1, 150, 50, 1, 0),
            makeSample(secondBoundary - 1, 150, 60, 0, 0),
            makeSample(secondBoundary, 150, 70, 1, 0),
            makeSample(secondBoundary + 1, 150, 80, 0, 0),
        });

    test.require(snapshots.size() == 3,
                 "incremental non-divisible mode splits at computed boundaries");
    if (snapshots.size() != 3) {
        return;
    }

    test.require(snapshots[0]->firstSampleIndex == 0
                     && snapshots[0]->lastSampleIndex == firstBoundary - 1,
                 "first incremental window range ends before boundary");
    test.require(snapshots[1]->firstSampleIndex == firstBoundary
                     && snapshots[1]->lastSampleIndex == secondBoundary - 1,
                 "second incremental window range starts at first boundary");
    test.require(snapshots[2]->firstSampleIndex == secondBoundary
                     && snapshots[2]->lastSampleIndex == secondBoundary + 1,
                 "third incremental window range starts at second boundary");
}

void testIncrementalWindowFallsBackOnBackwardSampleIndex(TestRunner& test)
{
    pipeline::SpectrumAggregator aggregator(nonDivisibleIncrementalConfig());

    const auto consumed = aggregator.consume(std::vector<core::SignalSample>{
        makeSample(100, 150, 60, 0, 0),
        makeSample(101, 150, 70, 1, 0),
        makeSample(90, 150, 80, 0, 0),
    });
    const auto flushed = aggregator.flush();

    test.require(consumed.usedIncrementalWindowIndex,
                 "backward sample test uses incremental mode");
    test.require(consumed.incrementalWindowFallbacks > 0,
                 "backward sample index triggers incremental exact fallback");
    test.require(consumed.snapshots.size() + flushed.snapshots.size() == 2,
                 "backward fallback still produces valid closed snapshots");
}

void testFastBinBoundariesMatchLegacy(TestRunner& test)
{
    pipeline::SpectrumAggregator legacyAggregator(legacyConfig());
    pipeline::SpectrumAggregator fastAggregator(fastConfig());
    const std::vector<core::SignalSample> samples{
        makeSample(0, 99, 80, 0, 0),
        makeSample(1, 100, 30, 0, 0),
        makeSample(2, 150, 50, 1, 1),
        makeSample(3, 200, 70, 0, 1),
        makeSample(4, 201, 80, 1, 0),
    };

    const auto legacySnapshot = consumeAndFlush(legacyAggregator, samples);
    const auto fastSnapshot = consumeAndFlush(fastAggregator, samples);

    test.require(legacySnapshot && fastSnapshot,
                 "fast and legacy bin modes both publish snapshots");
    if (!legacySnapshot || !fastSnapshot) {
        return;
    }

    requireSameBins(test, *legacySnapshot, *fastSnapshot, "fast/legacy bin boundary");
    requireSameBandSummaries(test,
                             *legacySnapshot,
                             *fastSnapshot,
                             "fast/legacy bin boundary");
    test.require(legacySnapshot->counters.outOfRangeSamples == 2
                     && fastSnapshot->counters.outOfRangeSamples == 2,
                 "fast and legacy bin modes count out-of-range samples equally");
}

} // namespace

int main()
{
    TestRunner test;

    testFrequencyMapsToExpectedBin(test);
    testBeamPeaksAndTotalPeak(test);
    testBandSummaries(test);
    testInvalidAndOutOfRangeSamplesAreCounted(test);
    testSnapshotPeriodPreventsPerBlockSpam(test);
    testSequenceIncreasesAcrossWindows(test);
    testFastAndLegacyModesProduceEquivalentSnapshot(test);
    testFixedBandSummaryRejectsOutOfCapacityBand(test);
    testFastWindowBoundariesMatchLegacy(test);
    testIncrementalWindowMatchesExactWhenPeriodIsNotDivisible(test);
    testIncrementalWindowSplitsAtNonDivisibleBoundaries(test);
    testIncrementalWindowFallsBackOnBackwardSampleIndex(test);
    testFastBinBoundariesMatchLegacy(test);

    return test.result();
}
