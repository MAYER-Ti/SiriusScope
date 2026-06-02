#include "pipeline/bearing_aggregator.h"

#include <algorithm>
#include <cmath>
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

    void requireNear(double actual,
                     double expected,
                     double tolerance,
                     const std::string& message)
    {
        require(std::abs(actual - expected) <= tolerance, message);
    }

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

pipeline::BearingAggregatorConfig makeConfig()
{
    pipeline::BearingAggregatorConfig config;
    config.frequencyBinCount = 11;
    config.sourceMinHz = 100;
    config.sourceMaxHz = 200;
    config.windowPeriodNs = 20'000'000;
    config.amplitudeFloor = 1;
    config.beamHalfSeparationDeg = 30.0;
    config.minQuality = 0.0;
    config.fallbackAntennaAzimuthDeg = 45.0;
    config.timeBase = core::TimeBase{1'000'000'000, 0, 1'000'000};
    return config;
}

pipeline::BearingAggregatorConfig makeStorageConfig(
    pipeline::BearingCandidateStorageMode storageMode)
{
    auto config = makeConfig();
    config.candidateStorageMode = storageMode;
    config.bandCapacity = 4;
    config.frequencyBinCount = 16;
    return config;
}

core::SignalSample sample(std::uint64_t sampleIndex,
                          std::int64_t frequencyHz,
                          int beamIndex,
                          int amplitude,
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

std::shared_ptr<const pipeline::BearingSnapshot> consumeAndFlush(
    pipeline::BearingAggregator& aggregator,
    std::vector<core::SignalSample> samples,
    double antennaAzimuthDeg = 45.0)
{
    pipeline::SignalBlock block(samples.size());
    pipeline::SignalBlockMetadata metadata;
    metadata.firstSampleIndex = samples.empty() ? 0 : samples.front().sampleIndex;
    metadata.lastSampleIndex = samples.empty() ? 0 : samples.back().sampleIndex;
    metadata.antennaAzimuthDeg = antennaAzimuthDeg;
    block.reset(metadata);
    block.assignSamples(samples);
    aggregator.consume(block);
    const auto flushed = aggregator.flush();
    return flushed.snapshots.empty() ? nullptr : flushed.snapshots.front();
}

void testEqualBeamsPointAtAntennaAzimuth(TestRunner& test)
{
    pipeline::BearingAggregator aggregator(makeConfig());
    const auto snapshot =
        consumeAndFlush(aggregator,
                        {sample(0, 150, 0, 80), sample(0, 150, 1, 80)},
                        45.0);

    test.require(snapshot != nullptr, "equal beams publish bearing snapshot");
    test.require(snapshot && snapshot->estimates.size() == 1,
                 "complete candidate creates one estimate");
    if (!snapshot || snapshot->estimates.empty()) {
        return;
    }

    test.require(std::abs(snapshot->estimates.front().bearingAzimuthDeg - 45.0) < 0.001,
                 "equal beams estimate bearing near antenna azimuth");
    test.require(snapshot->estimates.front().beam0Peak == 80
                     && snapshot->estimates.front().beam1Peak == 80,
                 "estimate stores beam peaks separately");
}

void testBeamDominanceMovesBearingTowardBeamAxis(TestRunner& test)
{
    pipeline::BearingAggregator beam0Aggregator(makeConfig());
    const auto beam0Snapshot =
        consumeAndFlush(beam0Aggregator,
                        {sample(0, 150, 0, 100), sample(0, 150, 1, 50)},
                        45.0);

    pipeline::BearingAggregator beam1Aggregator(makeConfig());
    const auto beam1Snapshot =
        consumeAndFlush(beam1Aggregator,
                        {sample(0, 150, 0, 50), sample(0, 150, 1, 100)},
                        45.0);

    test.require(beam0Snapshot && !beam0Snapshot->estimates.empty(),
                 "beam0-dominant candidate creates estimate");
    test.require(beam1Snapshot && !beam1Snapshot->estimates.empty(),
                 "beam1-dominant candidate creates estimate");
    if (!beam0Snapshot || beam0Snapshot->estimates.empty() || !beam1Snapshot
        || beam1Snapshot->estimates.empty()) {
        return;
    }

    test.require(beam0Snapshot->estimates.front().bearingAzimuthDeg < 45.0,
                 "beam0 dominance moves bearing toward beam0 axis");
    test.require(beam1Snapshot->estimates.front().bearingAzimuthDeg > 45.0,
                 "beam1 dominance moves bearing toward beam1 axis");
}

void testMissingBeamIsCountedWithoutEstimate(TestRunner& test)
{
    pipeline::BearingAggregator aggregator(makeConfig());
    const auto snapshot =
        consumeAndFlush(aggregator,
                        {
                            sample(0, 150, 0, 90),
                            sample(1, 160, 0, 70),
                        },
                        45.0);

    test.require(snapshot != nullptr, "incomplete candidates still publish summary snapshot");
    test.require(snapshot && snapshot->estimates.empty(),
                 "missing beam candidates do not create estimates");
    test.require(snapshot && snapshot->counters.incompleteCandidates == 2,
                 "incomplete candidates are counted");
    test.require(snapshot && snapshot->counters.missingBeam1Candidates == 2,
                 "missing beam 1 candidates are counted");
    test.require(aggregator.counters().incompleteCandidates == 2,
                 "aggregator keeps incomplete candidate counters");
}

void testManySamplesInOneWindowDoNotCreatePerCandidateSpam(TestRunner& test)
{
    pipeline::BearingAggregator aggregator(makeConfig());
    std::vector<core::SignalSample> samples;
    for (std::uint64_t index = 0; index < 20; ++index) {
        samples.push_back(sample(index, 150, 0, 60));
    }

    const auto consumed = aggregator.consume(samples);
    const auto flushed = aggregator.flush();

    test.require(consumed.snapshots.empty(),
                 "same bearing window does not publish per-sample snapshots");
    test.require(flushed.snapshots.size() == 1,
                 "flush publishes one aggregated incomplete summary");
    test.require(flushed.snapshots.front()->estimates.empty(),
                 "one-beam samples do not produce estimates");
    test.require(flushed.snapshots.front()->counters.incompleteCandidates == 1,
                 "many one-beam samples collapse into one candidate counter");
}

void testSequenceIsImmutableAndIncreases(TestRunner& test)
{
    pipeline::BearingAggregator aggregator(makeConfig());
    const auto consumed = aggregator.consume(std::vector<core::SignalSample>{
        sample(0, 150, 0, 80),
        sample(0, 150, 1, 80),
        sample(20, 150, 0, 90),
        sample(20, 150, 1, 90),
    });
    const auto flushed = aggregator.flush();

    test.require(consumed.snapshots.size() == 1 && flushed.snapshots.size() == 1,
                 "window change and flush publish two bearing snapshots");
    test.require(consumed.snapshots.front()->sequenceId == 1
                     && flushed.snapshots.front()->sequenceId == 2,
                 "bearing snapshot sequence increases");
    static_assert(std::is_const_v<
                  std::remove_reference_t<decltype(*consumed.snapshots.front())>>,
                  "Bearing snapshots are exposed as immutable shared_ptr<const T>");
}

void testMapAndFlatStorageProduceEquivalentEstimate(TestRunner& test)
{
    pipeline::BearingAggregator mapAggregator(
        makeStorageConfig(pipeline::BearingCandidateStorageMode::MapByBandAndBin));
    pipeline::BearingAggregator flatAggregator(
        makeStorageConfig(pipeline::BearingCandidateStorageMode::FlatBandBinVector));

    const std::vector<core::SignalSample> samples{
        sample(0, 150, 0, 100),
        sample(1, 150, 1, 80),
    };
    const auto mapSnapshot = consumeAndFlush(mapAggregator, samples, 45.0);
    const auto flatSnapshot = consumeAndFlush(flatAggregator, samples, 45.0);

    test.require(mapSnapshot && flatSnapshot,
                 "map and flat storage both publish equivalent snapshots");
    test.require(mapSnapshot && flatSnapshot
                     && mapSnapshot->estimates.size() == flatSnapshot->estimates.size(),
                 "map and flat storage estimate counts match");
    if (!mapSnapshot || !flatSnapshot || mapSnapshot->estimates.empty()
        || flatSnapshot->estimates.empty()) {
        return;
    }

    const auto& mapEstimate = mapSnapshot->estimates.front();
    const auto& flatEstimate = flatSnapshot->estimates.front();
    test.require(mapEstimate.bandIndex == flatEstimate.bandIndex,
                 "map and flat storage keep band index");
    test.require(mapEstimate.frequencyBin == flatEstimate.frequencyBin,
                 "map and flat storage keep frequency bin");
    test.require(mapEstimate.frequencyHz == flatEstimate.frequencyHz,
                 "map and flat storage keep center frequency");
    test.requireNear(flatEstimate.bearingAzimuthDeg,
                     mapEstimate.bearingAzimuthDeg,
                     0.0001,
                     "map and flat storage keep bearing");
    test.requireNear(flatEstimate.quality,
                     mapEstimate.quality,
                     0.0001,
                     "map and flat storage keep quality");
}

void testFlatStorageRejectsOutOfCapacityBand(TestRunner& test)
{
    auto config = makeConfig();
    config.candidateStorageMode =
        pipeline::BearingCandidateStorageMode::FlatBandBinVector;
    config.bandCapacity = 1;

    pipeline::BearingAggregator aggregator(config);
    const auto consumed = aggregator.consume(std::vector<core::SignalSample>{
        sample(0, 150, 0, 80, 2),
        sample(0, 150, 1, 80, 2),
    });
    const auto flushed = aggregator.flush();

    test.require(consumed.snapshots.empty() && flushed.snapshots.empty(),
                 "flat storage rejects out-of-capacity band without candidate");
    test.require(aggregator.counters().invalidSamples == 2,
                 "flat storage counts out-of-capacity band as invalid samples");
}

void testFastWindowSplitsAtDivisibleBoundary(TestRunner& test)
{
    pipeline::BearingAggregator aggregator(makeConfig());
    const auto consumed = aggregator.consume(std::vector<core::SignalSample>{
        sample(19, 150, 0, 80),
        sample(19, 150, 1, 80),
        sample(20, 150, 0, 90),
        sample(20, 150, 1, 90),
    });
    const auto flushed = aggregator.flush();

    test.require(consumed.snapshots.size() == 1 && flushed.snapshots.size() == 1,
                 "fast divisible window path splits at configured boundary");
    test.require(consumed.snapshots.front()->estimates.size() == 1
                     && flushed.snapshots.front()->estimates.size() == 1,
                 "fast divisible windows keep complete estimates");
    test.require(consumed.snapshots.front()->estimates.front().sampleIndex
                         == 19
                     && flushed.snapshots.front()->estimates.front().sampleIndex
                         == 20,
                 "fast divisible window path preserves representative indexes");
}

void testExactFallbackWindowSplitsAtNonDivisibleBoundary(TestRunner& test)
{
    auto config = makeConfig();
    config.timeBase = core::TimeBase{1'000'000'000, 0, 3};
    config.windowPeriodNs = 10;

    pipeline::BearingAggregator aggregator(config);
    const auto consumed = aggregator.consume(std::vector<core::SignalSample>{
        sample(3, 150, 0, 80),
        sample(3, 150, 1, 80),
        sample(4, 150, 0, 90),
        sample(4, 150, 1, 90),
    });
    const auto flushed = aggregator.flush();

    test.require(consumed.snapshots.size() == 1 && flushed.snapshots.size() == 1,
                 "exact fallback window path splits non-divisible periods");
    test.require(consumed.snapshots.front()->estimates.front().sampleIndex
                         == 3
                     && flushed.snapshots.front()->estimates.front().sampleIndex
                         == 4,
                 "exact fallback window path preserves representative indexes");
}

void testFastBinBoundariesAndOutOfRangeSamples(TestRunner& test)
{
    auto config = makeConfig();
    config.frequencyBinCount = 11;
    config.candidateStorageMode =
        pipeline::BearingCandidateStorageMode::FlatBandBinVector;

    pipeline::BearingAggregator aggregator(config);
    const auto snapshot = consumeAndFlush(aggregator,
                                          {
                                              sample(0, 100, 0, 80),
                                              sample(0, 100, 1, 80),
                                              sample(1, 150, 0, 80),
                                              sample(1, 150, 1, 80),
                                              sample(2, 200, 0, 80),
                                              sample(2, 200, 1, 80),
                                          });

    test.require(snapshot && snapshot->estimates.size() == 3,
                 "fast bin path creates candidates for min/mid/max frequencies");
    if (!snapshot || snapshot->estimates.size() != 3) {
        return;
    }

    std::vector<std::uint32_t> bins;
    for (const auto& estimate : snapshot->estimates) {
        bins.push_back(estimate.frequencyBin);
    }
    std::sort(bins.begin(), bins.end());
    test.require(bins == std::vector<std::uint32_t>{0, 5, 10},
                 "fast bin path maps min/mid/max to expected bins");

    pipeline::BearingAggregator outOfRangeAggregator(config);
    const auto outOfRangeConsumed =
        outOfRangeAggregator.consume(std::vector<core::SignalSample>{
            sample(0, 99, 0, 80),
            sample(0, 99, 1, 80),
            sample(1, 201, 0, 80),
            sample(1, 201, 1, 80),
        });
    const auto outOfRangeFlushed = outOfRangeAggregator.flush();

    test.require(outOfRangeConsumed.snapshots.empty()
                     && outOfRangeFlushed.snapshots.empty(),
                 "out-of-range frequencies create no bearing candidates");
    test.require(outOfRangeAggregator.counters().outOfRangeSamples == 4,
                 "out-of-range frequencies are counted");
}

} // namespace

int main()
{
    TestRunner test;

    testEqualBeamsPointAtAntennaAzimuth(test);
    testBeamDominanceMovesBearingTowardBeamAxis(test);
    testMissingBeamIsCountedWithoutEstimate(test);
    testManySamplesInOneWindowDoNotCreatePerCandidateSpam(test);
    testSequenceIsImmutableAndIncreases(test);
    testMapAndFlatStorageProduceEquivalentEstimate(test);
    testFlatStorageRejectsOutOfCapacityBand(test);
    testFastWindowSplitsAtDivisibleBoundary(test);
    testExactFallbackWindowSplitsAtNonDivisibleBoundary(test);
    testFastBinBoundariesAndOutOfRangeSamples(test);

    return test.result();
}
