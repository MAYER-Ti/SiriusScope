#include "pipeline/bearing_aggregator.h"

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

} // namespace

int main()
{
    TestRunner test;

    testEqualBeamsPointAtAntennaAzimuth(test);
    testBeamDominanceMovesBearingTowardBeamAxis(test);
    testMissingBeamIsCountedWithoutEstimate(test);
    testManySamplesInOneWindowDoNotCreatePerCandidateSpam(test);
    testSequenceIsImmutableAndIncreases(test);

    return test.result();
}
