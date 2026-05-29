#include "pipeline/spectrum_aggregator.h"

#include <cstdlib>
#include <iostream>
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

    return test.result();
}
