#include "pipeline/signal_parameter_aggregator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
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

pipeline::SignalParameterAggregationResult consumeIndexes(
    pipeline::SignalParameterAggregator& aggregator,
    std::initializer_list<std::uint64_t> sampleIndexes)
{
    std::vector<core::SignalSample> samples;
    samples.reserve(sampleIndexes.size());
    for (const auto sampleIndex : sampleIndexes) {
        samples.push_back(sample(sampleIndex));
    }
    return aggregator.consume(samples);
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

    test.require(result.usedTrustedFixedBandFastPath,
                 "one-pulse default consume uses trusted fixed-band fast path");
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

    test.require(result.usedTrustedFixedBandFastPath,
                 "streaming default uses trusted fixed-band fast path");
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

void testDefaultAggregatorRejectsOutOfCapacityBand(TestRunner& test)
{
    const auto defaultBandCount = core::defaultRuntimeCapabilities().bandCount;
    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    const std::vector<core::SignalSample> samples{
        sample(10, 0),
        sample(11, defaultBandCount),
    };

    const auto result = aggregator.consume(samples);
    const auto snapshot = result.snapshot;

    test.require(result.usedTrustedFixedBandFastPath,
                 "out-of-capacity default uses trusted fixed-band fast path");
    test.require(snapshot != nullptr, "out-of-capacity consume publishes snapshot");
    test.require(result.acceptedSampleDelta == 1,
                 "default trusted vector accepts in-capacity band sample");
    test.require(result.rejectedSampleDelta == 1,
                 "default trusted vector rejects out-of-capacity band sample");
    test.require(snapshot && snapshot->acceptedSampleCount == 1,
                 "out-of-capacity snapshot counts accepted samples");
    test.require(snapshot && snapshot->rejectedSampleCount == 1,
                 "out-of-capacity snapshot counts rejected samples");
    test.require(snapshot && snapshot->bands.size() == 1,
                 "out-of-capacity snapshot excludes invalid band");
    test.require(snapshot && findBand(*snapshot, defaultBandCount) == nullptr,
                 "out-of-capacity snapshot has no invalid band summary");
}

void testStreamingIngestHandlesLargeMonotonicBlock(TestRunner& test)
{
    constexpr std::uint64_t sampleCount = 50'000;
    constexpr int bandCount = 4;

    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    std::vector<core::SignalSample> samples;
    samples.reserve(sampleCount);
    for (std::uint64_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const auto bandIndex = static_cast<int>(sampleIndex % bandCount);
        samples.push_back(sample(sampleIndex,
                                 bandIndex,
                                 3'000'000'000LL
                                     + static_cast<std::int64_t>(bandIndex)
                                         * 100'000'000LL));
    }

    const auto result = aggregator.consume(samples);
    const auto snapshot = result.snapshot;

    test.require(result.usedTrustedFixedBandFastPath,
                 "large streaming block uses trusted fixed-band fast path");
    test.require(snapshot != nullptr, "large streaming block publishes snapshot");
    test.require(result.acceptedSampleDelta == sampleCount,
                 "large streaming block accepts all sample deltas");
    test.require(result.rejectedSampleDelta == 0,
                 "large streaming block rejects no monotonic samples");
    test.require(snapshot && snapshot->acceptedSampleCount == sampleCount,
                 "large streaming block snapshot counts accepted samples");
}

void testLegacyModesDoNotUseTrustedFixedBandFastPath(TestRunner& test)
{
    auto validatedConfig = microsecondConfig();
    validatedConfig.estimatorConfig.validationMode =
        processing::SignalParameterValidationMode::ValidateEachSample;
    pipeline::SignalParameterAggregator validated(validatedConfig);
    const std::vector<core::SignalSample> validatedSamples{sample(10)};
    const auto validatedResult = validated.consume(validatedSamples);

    auto trustedMapConfig = microsecondConfig();
    trustedMapConfig.estimatorConfig.bandStateMode =
        processing::SignalParameterBandStateMode::MapByBandIndex;
    pipeline::SignalParameterAggregator trustedMap(trustedMapConfig);
    const std::vector<core::SignalSample> trustedMapSamples{sample(10)};
    const auto trustedMapResult = trustedMap.consume(trustedMapSamples);

    auto sortedConfig = microsecondConfig();
    sortedConfig.estimatorConfig.ingestMode =
        processing::SignalParameterIngestMode::SortByBandAndSample;
    pipeline::SignalParameterAggregator sorted(sortedConfig);
    const std::vector<core::SignalSample> sortedSamples{sample(10)};
    const auto sortedResult = sorted.consume(sortedSamples);

    test.require(!validatedResult.usedTrustedFixedBandFastPath,
                 "validated streaming mode does not use trusted fixed-band fast path");
    test.require(!trustedMapResult.usedTrustedFixedBandFastPath,
                 "trusted map mode does not use trusted fixed-band fast path");
    test.require(!sortedResult.usedTrustedFixedBandFastPath,
                 "sorted mode does not use trusted fixed-band fast path");
}

void testSnapshotCadenceThrottlesAfterInitialSnapshot(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::WallClockPeriod;
    config.snapshotPeriod = std::chrono::hours{1};
    config.publishSnapshotEveryBlock = false;
    pipeline::SignalParameterAggregator aggregator(config);
    const std::vector<core::SignalSample> firstBlock{sample(10)};
    const std::vector<core::SignalSample> secondBlock{sample(20)};

    const auto first = aggregator.consume(firstBlock);
    const auto second = aggregator.consume(secondBlock);
    const auto forced = aggregator.forceSnapshot();

    test.require(first.snapshot != nullptr,
                 "cadence publishes first non-empty signal parameter snapshot");
    test.require(second.snapshot == nullptr,
                 "cadence suppresses immediate second signal parameter snapshot");
    test.require(second.acceptedSampleDelta == 1,
                 "cadence does not suppress accepted sample delta");
    test.require(forced != nullptr, "forceSnapshot returns snapshot after throttled consume");
    test.require(forced && forced->acceptedSampleCount == 2,
                 "forceSnapshot includes throttled samples");
}

void testPublishSnapshotEveryBlockPreservesOldBehavior(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::WallClockPeriod;
    config.snapshotPeriod = std::chrono::hours{1};
    config.publishSnapshotEveryBlock = true;
    pipeline::SignalParameterAggregator aggregator(config);
    const std::vector<core::SignalSample> firstBlock{sample(10)};
    const std::vector<core::SignalSample> secondBlock{sample(20)};

    const auto first = aggregator.consume(firstBlock);
    const auto second = aggregator.consume(secondBlock);

    test.require(first.snapshot != nullptr,
                 "publishSnapshotEveryBlock publishes first snapshot");
    test.require(second.snapshot != nullptr,
                 "publishSnapshotEveryBlock publishes second snapshot");
}

void testNonPositiveSnapshotPeriodPublishesEveryBlock(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::WallClockPeriod;
    config.snapshotPeriod = std::chrono::milliseconds{0};
    pipeline::SignalParameterAggregator aggregator(config);
    const std::vector<core::SignalSample> firstBlock{sample(10)};
    const std::vector<core::SignalSample> secondBlock{sample(20)};

    const auto first = aggregator.consume(firstBlock);
    const auto second = aggregator.consume(secondBlock);

    test.require(first.snapshot != nullptr,
                 "zero snapshot period publishes first snapshot");
    test.require(second.snapshot != nullptr,
                 "zero snapshot period publishes every block");
}

void testForceSnapshotReturnsLatestAccumulatedData(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::WallClockPeriod;
    config.snapshotPeriod = std::chrono::hours{1};
    pipeline::SignalParameterAggregator aggregator(config);
    const std::vector<core::SignalSample> firstBlock{sample(10), sample(11)};
    const std::vector<core::SignalSample> secondBlock{sample(20), sample(21), sample(22)};

    (void) aggregator.consume(firstBlock);
    const auto throttled = aggregator.consume(secondBlock);
    const auto snapshot = aggregator.forceSnapshot();
    const auto* band0 = snapshot ? findBand(*snapshot, 0) : nullptr;

    test.require(throttled.snapshot == nullptr,
                 "forceSnapshot test has throttled intermediate consume");
    test.require(snapshot != nullptr, "forceSnapshot returns latest snapshot");
    test.require(snapshot && snapshot->acceptedSampleCount == 5,
                 "forceSnapshot counts all accumulated samples");
    test.require(band0 != nullptr, "forceSnapshot contains accumulated band");
    if (!band0) {
        return;
    }

    test.require(band0->pulseCount == 2,
                 "forceSnapshot counts pulses from throttled block");
    test.require(band0->pulseWidthUs && nearly(*band0->pulseWidthUs, 2.5),
                 "forceSnapshot calculates latest average PW");
    test.require(band0->pulseRepetitionPeriodUs
                     && nearly(*band0->pulseRepetitionPeriodUs, 10.0),
                 "forceSnapshot calculates latest PRI");
}

void testStreamingSinglePassSpanIgnoresRejectedSamples(TestRunner& test)
{
    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    const std::vector<core::SignalSample> samples{sample(10), sample(12), sample(9)};
    const auto result = aggregator.consume(samples);
    const auto snapshot = aggregator.forceSnapshot();
    const auto* band0 = snapshot ? findBand(*snapshot, 0) : nullptr;

    test.require(result.acceptedSampleDelta == 2,
                 "single-pass span test accepts monotonic samples");
    test.require(result.usedTrustedFixedBandFastPath,
                 "single-pass span test uses trusted fixed-band fast path");
    test.require(result.rejectedSampleDelta == 1,
                 "single-pass span test rejects out-of-order sample");
    test.require(band0 != nullptr, "single-pass span snapshot contains band 0");
    if (!band0) {
        return;
    }

    test.require(band0->firstSampleIndex == 10 && band0->lastSampleIndex == 12,
                 "single-pass span ignores rejected out-of-order sample");
}

void testProcessedBlockIntervalPolicy(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::ProcessedBlockInterval;
    config.snapshotBlockInterval = 3;
    config.publishSnapshotEveryBlock = false;
    pipeline::SignalParameterAggregator aggregator(config);

    const auto first = consumeIndexes(aggregator, {10});
    const auto second = consumeIndexes(aggregator, {20});
    const auto third = consumeIndexes(aggregator, {30});
    const auto fourth = consumeIndexes(aggregator, {40});

    test.require(first.snapshot != nullptr,
                 "block interval policy publishes forced first snapshot");
    test.require(second.snapshot == nullptr,
                 "block interval policy suppresses first block after snapshot");
    test.require(third.snapshot == nullptr,
                 "block interval policy suppresses second block after snapshot");
    test.require(fourth.snapshot != nullptr,
                 "block interval policy publishes after three further blocks");
}

void testManualOnlyPolicyPublishesOnlyForcedSnapshots(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::ManualOnly;
    pipeline::SignalParameterAggregator aggregator(config);

    const auto first = consumeIndexes(aggregator, {10});
    const auto second = consumeIndexes(aggregator, {20, 21});
    const auto forced = aggregator.forceSnapshot();

    test.require(first.snapshot != nullptr,
                 "manual policy still publishes forced first snapshot");
    test.require(second.snapshot == nullptr,
                 "manual policy suppresses periodic snapshots");
    test.require(forced != nullptr, "manual policy forceSnapshot returns snapshot");
    test.require(forced && forced->acceptedSampleCount == 3,
                 "manual policy forceSnapshot includes accumulated samples");
}

void testPublishEveryBlockOverridesManualPolicy(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::ManualOnly;
    config.publishSnapshotEveryBlock = true;
    pipeline::SignalParameterAggregator aggregator(config);

    const auto first = consumeIndexes(aggregator, {10});
    const auto second = consumeIndexes(aggregator, {20});
    const auto third = consumeIndexes(aggregator, {30});

    test.require(first.snapshot != nullptr,
                 "publish every block override publishes first snapshot");
    test.require(second.snapshot != nullptr,
                 "publish every block override publishes second snapshot");
    test.require(third.snapshot != nullptr,
                 "publish every block override publishes third snapshot");
}

void testSourceTimePolicyUsesAcceptedSampleTime(TestRunner& test)
{
    auto config = microsecondConfig();
    config.estimatorConfig.samplePeriodNs = 1'000'000;
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::SourceTimePeriod;
    config.sourceTimeSnapshotPeriod = std::chrono::milliseconds{10};
    pipeline::SignalParameterAggregator aggregator(config);

    const auto first = consumeIndexes(aggregator, {0, 1, 2});
    const auto second = consumeIndexes(aggregator, {3, 4, 5});
    const auto third = consumeIndexes(aggregator, {10, 11, 12});

    test.require(first.snapshot != nullptr,
                 "source-time policy publishes forced first snapshot");
    test.require(second.snapshot == nullptr,
                 "source-time policy suppresses short source-time delta");
    test.require(third.snapshot != nullptr,
                 "source-time policy publishes after configured source-time delta");
}

void testTimingFieldsAreReported(TestRunner& test)
{
    pipeline::SignalParameterAggregator aggregator(microsecondConfig());
    const auto result = consumeIndexes(aggregator, {10, 11, 12});

    test.require(result.snapshot != nullptr,
                 "timing test publishes initial snapshot");
    test.require(result.snapshotPublished,
                 "timing test marks actual snapshot publication");
    test.require(result.timing.total >= result.timing.ingest,
                 "timing test records total duration including ingest");
    test.require(result.timing.total >= result.timing.snapshotDecision,
                 "timing test records snapshot decision duration");
}

void testNoSnapshotLeavesFinalizeAndBuildTimingZero(TestRunner& test)
{
    auto config = microsecondConfig();
    config.snapshotPolicy = pipeline::SignalParameterSnapshotPolicy::ProcessedBlockInterval;
    config.snapshotBlockInterval = 1000;
    pipeline::SignalParameterAggregator aggregator(config);

    (void) consumeIndexes(aggregator, {10});
    const auto second = consumeIndexes(aggregator, {20});

    test.require(second.snapshot == nullptr,
                 "no-snapshot timing test suppresses second snapshot");
    test.require(!second.snapshotPublished,
                 "no-snapshot timing test marks no snapshot publication");
    test.require(second.timing.finalize == std::chrono::steady_clock::duration{},
                 "no-snapshot timing test leaves finalize timing zero");
    test.require(second.timing.snapshotBuild == std::chrono::steady_clock::duration{},
                 "no-snapshot timing test leaves snapshot build timing zero");
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
    testDefaultAggregatorRejectsOutOfCapacityBand(test);
    testStreamingIngestHandlesLargeMonotonicBlock(test);
    testLegacyModesDoNotUseTrustedFixedBandFastPath(test);
    testSnapshotCadenceThrottlesAfterInitialSnapshot(test);
    testPublishSnapshotEveryBlockPreservesOldBehavior(test);
    testNonPositiveSnapshotPeriodPublishesEveryBlock(test);
    testForceSnapshotReturnsLatestAccumulatedData(test);
    testStreamingSinglePassSpanIgnoresRejectedSamples(test);
    testProcessedBlockIntervalPolicy(test);
    testManualOnlyPolicyPublishesOnlyForcedSnapshots(test);
    testPublishEveryBlockOverridesManualPolicy(test);
    testSourceTimePolicyUsesAcceptedSampleTime(test);
    testTimingFieldsAreReported(test);
    testNoSnapshotLeavesFinalizeAndBuildTimingZero(test);

    return test.result();
}
