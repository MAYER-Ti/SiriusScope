#include "pipeline/waterfall_aggregator.h"

#include <cstdlib>
#include <iostream>
#include <iterator>
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

pipeline::WaterfallAggregatorConfig makeConfig()
{
    pipeline::WaterfallAggregatorConfig config;
    config.renderBinCount = 11;
    config.sourceMinHz = 100;
    config.sourceMaxHz = 200;
    config.rowPeriodNs = 20'000'000;
    config.timeBase = core::TimeBase{1'000'000'000, 0, 1'000'000};
    return config;
}

core::SignalSample sample(std::uint64_t sampleIndex,
                          std::int64_t frequencyHz,
                          int beamIndex,
                          int amplitude)
{
    return core::SignalSample{
        sampleIndex,
        0,
        0,
        frequencyHz,
        amplitude,
        beamIndex,
    };
}

std::vector<pipeline::WaterfallSnapshotRow> consumeAndFlush(
    pipeline::WaterfallAggregator& aggregator,
    std::vector<core::SignalSample> samples)
{
    auto consumed = aggregator.consume(samples);
    auto flushed = aggregator.flush();
    consumed.rows.insert(consumed.rows.end(),
                         std::make_move_iterator(flushed.rows.begin()),
                         std::make_move_iterator(flushed.rows.end()));
    return consumed.rows;
}

void testFrequencyMapsToExpectedBin(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());

    const auto rows = consumeAndFlush(aggregator, {sample(0, 150, 0, 42)});

    test.require(rows.size() == 1, "aggregator produces one row for one bucket");
    test.require(rows.front().cells.size() == 11, "row has configured render bin count");
    test.require(rows.front().cells[5].beam0Peak == 42,
                 "frequency in the middle maps to middle bin");
}

void testOneTimeBucketProducesOneRow(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());

    const auto rows = consumeAndFlush(aggregator,
                                      {sample(0, 150, 0, 20),
                                       sample(10, 151, 0, 30),
                                       sample(19, 152, 0, 40)});

    test.require(rows.size() == 1, "samples inside one time bucket produce one row");
    test.require(rows.front().firstSampleIndex == 0 && rows.front().lastSampleIndex == 19,
                 "row tracks sample index range inside the bucket");
}

void testDifferentTimeBucketsProduceMultipleRows(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());

    auto consumed = aggregator.consume(
        std::vector<core::SignalSample>{sample(0, 150, 0, 20), sample(20, 150, 0, 30)});
    auto flushed = aggregator.flush();

    test.require(consumed.rows.size() == 1, "new bucket closes previous row");
    test.require(flushed.rows.size() == 1, "flush closes the last open bucket");
    test.require(consumed.rows.front().utcNs == 1'000'000'000,
                 "first bucket UTC follows time base");
    test.require(flushed.rows.front().utcNs == 1'020'000'000,
                 "second bucket UTC advances by row period");
}

void testTimeBaseMapsSampleIndexToRowUtcMs(TestRunner& test)
{
    auto config = makeConfig();
    config.timeBase =
        core::TimeBase{1'700'000'000'000'000'000LL, 1000, 1'000'000};
    config.rowPeriodNs = 20'000'000;
    pipeline::WaterfallAggregator aggregator(config);

    const auto rows = consumeAndFlush(aggregator,
                                      {sample(1000, 150, 0, 20),
                                       sample(1010, 150, 1, 30),
                                       sample(1020, 150, 0, 40),
                                       sample(1040, 150, 1, 50)});

    test.require(rows.size() == 3,
                 "timebase test produces one row per 20 ms bucket");
    if (rows.size() != 3) {
        return;
    }

    const auto utcMs = [](const auto& row) {
        return row.utcNs / 1'000'000;
    };

    test.require(utcMs(rows[0]) == 1'700'000'000'000LL,
                 "first waterfall row UTC ms equals recording start");
    test.require(utcMs(rows[0]) < utcMs(rows[1]) && utcMs(rows[1]) < utcMs(rows[2]),
                 "waterfall row UTC values are monotonic");
    test.require(utcMs(rows[1]) - utcMs(rows[0]) == 20
                     && utcMs(rows[2]) - utcMs(rows[1]) == 20,
                 "waterfall row UTC deltas match configured row period");
}

void testBeamPeaksAndHitCount(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());

    const auto rows = consumeAndFlush(aggregator,
                                      {sample(0, 150, 0, 10),
                                       sample(1, 150, 0, 40),
                                       sample(2, 150, 1, 50)});
    const auto& cell = rows.front().cells[5];

    test.require(cell.beam0Peak == 40, "beam 0 peak is aggregated separately");
    test.require(cell.beam1Peak == 50, "beam 1 peak is aggregated separately");
    test.require(cell.hitCount == 3, "hitCount counts samples in the aggregate cell");
}

void testDirectionalBalanceChangesAcrossBuckets(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());

    auto consumed = aggregator.consume(
        std::vector<core::SignalSample>{sample(0, 150, 0, 100),
                                        sample(0, 150, 1, 30),
                                        sample(20, 150, 0, 25),
                                        sample(20, 150, 1, 95)});
    auto flushed = aggregator.flush();

    test.require(consumed.rows.size() == 1 && flushed.rows.size() == 1,
                 "directional samples produce one row per time bucket");
    if (consumed.rows.empty() || flushed.rows.empty()) {
        return;
    }

    const auto& firstCell = consumed.rows.front().cells[5];
    const auto& secondCell = flushed.rows.front().cells[5];
    test.require(firstCell.beam0Peak > firstCell.beam1Peak,
                 "first waterfall bucket preserves beam0-dominant direction");
    test.require(secondCell.beam1Peak > secondCell.beam0Peak,
                 "second waterfall bucket preserves beam1-dominant direction");
}

void testAggregatorDoesNotCreateRowPerSampleIndex(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());
    std::vector<core::SignalSample> samples;
    for (std::uint64_t index = 0; index < 20; ++index) {
        samples.push_back(sample(index, 150, static_cast<int>(index % 2), 20));
    }

    const auto rows = consumeAndFlush(aggregator, std::move(samples));

    test.require(rows.size() == 1, "many sampleIndex values in one bucket produce one row");
}

void testSnapshotIsConstAndSequenceIncreases(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());

    auto firstRows = consumeAndFlush(aggregator, {sample(0, 150, 0, 20)});
    std::shared_ptr<const pipeline::WaterfallSnapshot> first =
        aggregator.makeSnapshot(std::move(firstRows));
    auto secondRows = consumeAndFlush(aggregator, {sample(20, 150, 1, 30)});
    std::shared_ptr<const pipeline::WaterfallSnapshot> second =
        aggregator.makeSnapshot(std::move(secondRows));

    test.require(first && second, "aggregator creates snapshots from closed rows");
    test.require(first->sequenceId + 1 == second->sequenceId,
                 "snapshot sequence id increases");
    test.require((std::is_const_v<std::remove_reference_t<decltype(*first)>>),
                 "snapshot handle exposes immutable snapshot");
}

void testCountersForOutOfRangeAndEmptyBlocks(TestRunner& test)
{
    pipeline::WaterfallAggregator aggregator(makeConfig());

    aggregator.consume(std::vector<core::SignalSample>{sample(0, 250, 0, 20),
                                                       sample(1, 0, 0, 20)});
    pipeline::SignalBlock emptyBlock(4);
    emptyBlock.reset();
    aggregator.consume(emptyBlock);
    const auto counters = aggregator.counters();

    test.require(counters.outOfRangeSamples == 1, "out-of-range frequency is counted");
    test.require(counters.invalidFrequencySamples == 1, "invalid frequency is counted");
    test.require(counters.emptyBlocks == 1, "empty blocks are counted");
}

} // namespace

int main()
{
    TestRunner test;

    testFrequencyMapsToExpectedBin(test);
    testOneTimeBucketProducesOneRow(test);
    testDifferentTimeBucketsProduceMultipleRows(test);
    testTimeBaseMapsSampleIndexToRowUtcMs(test);
    testBeamPeaksAndHitCount(test);
    testDirectionalBalanceChangesAcrossBuckets(test);
    testAggregatorDoesNotCreateRowPerSampleIndex(test);
    testSnapshotIsConstAndSequenceIncreases(test);
    testCountersForOutOfRangeAndEmptyBlocks(test);

    return test.result();
}
