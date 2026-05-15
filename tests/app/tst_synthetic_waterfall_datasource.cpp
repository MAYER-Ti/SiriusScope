#include "app/syntheticwaterfalldatasource.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>

namespace {

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

SyntheticWaterfallDataSource makeSource(double directionBias)
{
    SyntheticWaterfallSourceConfig config;
    config.binCount = 101;
    config.emitters = {SyntheticEmitter{0.5, 0.12, 0.8, directionBias, 0.0, 0.0}};
    return SyntheticWaterfallDataSource(config);
}

bool nonZero(const WaterfallBeamBin& bin)
{
    return bin.left > 0 || bin.right > 0;
}

int strongestIndex(const WaterfallRow& row)
{
    const auto strongest = std::max_element(row.bins.cbegin(), row.bins.cend(), [](auto lhs, auto rhs) {
        return std::max(lhs.left, lhs.right) < std::max(rhs.left, rhs.right);
    });
    return strongest == row.bins.cend() ? -1 : static_cast<int>(std::distance(row.bins.cbegin(), strongest));
}

double frequencyAtIndex(const WaterfallRow& row, int index)
{
    if (row.bins.size() <= 1 || index < 0) {
        return row.viewMinHz;
    }
    const double ratio = static_cast<double>(index) / static_cast<double>(row.bins.size() - 1);
    return row.viewMinHz + ratio * (row.viewMaxHz - row.viewMinHz);
}

void testDefaultConfigZerosOutsideBands(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(0.0);
    const QVector<SyntheticBandRange> bands = {SyntheticBandRange{150.0, 20.0}};
    const WaterfallRow row = source.nextRow(0, 0.0, 100.0, bands);

    test.require(!row.bins.isEmpty() && std::none_of(row.bins.cbegin(), row.bins.cend(), nonZero),
                 "default restricted source is zero outside BandItem ranges");
}

void testInsideBandHasSignal(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(0.0);
    const QVector<SyntheticBandRange> bands = {SyntheticBandRange{50.0, 40.0}};
    const WaterfallRow row = source.nextRow(0, 0.0, 100.0, bands);

    test.require(std::any_of(row.bins.cbegin(), row.bins.cend(), nonZero),
                 "inside BandItem range has non-zero bins");
}

void testSignalIsCenteredOnBand(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(0.0);
    const QVector<SyntheticBandRange> bands = {SyntheticBandRange{60.0, 30.0}};
    const WaterfallRow row = source.nextRow(0, 0.0, 100.0, bands);

    const double strongestHz = frequencyAtIndex(row, strongestIndex(row));
    test.require(std::abs(strongestHz - 60.0) <= 1.0,
                 "single BandItem signal is centered on band centerHz");
}

void testMovingBandMovesNextRowSignal(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(0.0);
    const WaterfallRow first = source.nextRow(0, 0.0, 100.0, {SyntheticBandRange{35.0, 20.0}});
    const WaterfallRow second = source.nextRow(1000, 0.0, 100.0, {SyntheticBandRange{75.0, 20.0}});

    const double firstHz = frequencyAtIndex(first, strongestIndex(first));
    const double secondHz = frequencyAtIndex(second, strongestIndex(second));

    test.require(std::abs(firstHz - 35.0) <= 1.0, "first row peak starts on first centerHz");
    test.require(std::abs(secondHz - 75.0) <= 1.0, "next row peak moves to updated centerHz");
}

void testOneDominantPeakPerBand(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(0.0);
    const QVector<SyntheticBandRange> bands = {
        SyntheticBandRange{25.0, 16.0},
        SyntheticBandRange{70.0, 20.0}
    };
    const WaterfallRow row = source.nextRow(0, 0.0, 100.0, bands);

    int peaks = 0;
    for (const double expectedHz : {25.0, 70.0}) {
        const int index = static_cast<int>(std::lround(expectedHz));
        bool found = false;
        for (int delta = -1; delta <= 1; ++delta) {
            const int bin = index + delta;
            if (bin >= 0 && bin < row.bins.size() && nonZero(row.bins.at(bin))) {
                found = true;
            }
        }
        if (found) {
            ++peaks;
        }
    }

    test.require(peaks == 2, "source creates one centered signal for each BandItem");
}

void testNegativeBiasGivesLeftDominance(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(-0.8);
    const QVector<SyntheticBandRange> bands = {SyntheticBandRange{50.0, 40.0}};
    const WaterfallRow row = source.nextRow(0, 0.0, 100.0, bands);
    const int strongest = strongestIndex(row);

    test.require(strongest >= 0 && row.bins.at(strongest).left > row.bins.at(strongest).right,
                 "negative bias gives left > right");
}

void testPositiveBiasGivesRightDominance(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(0.8);
    const QVector<SyntheticBandRange> bands = {SyntheticBandRange{50.0, 40.0}};
    const WaterfallRow row = source.nextRow(0, 0.0, 100.0, bands);
    const int strongest = strongestIndex(row);

    test.require(strongest >= 0 && row.bins.at(strongest).right > row.bins.at(strongest).left,
                 "positive bias gives right > left");
}

void testZeroBiasGivesEqualBeams(TestRunner& test)
{
    const SyntheticWaterfallDataSource source = makeSource(0.0);
    const QVector<SyntheticBandRange> bands = {SyntheticBandRange{50.0, 40.0}};
    const WaterfallRow row = source.nextRow(0, 0.0, 100.0, bands);
    const int strongest = strongestIndex(row);

    test.require(strongest >= 0 && row.bins.at(strongest).left == row.bins.at(strongest).right,
                 "zero bias gives equal left/right amplitudes");
}

} // namespace

int main()
{
    TestRunner test;

    testDefaultConfigZerosOutsideBands(test);
    testInsideBandHasSignal(test);
    testSignalIsCenteredOnBand(test);
    testMovingBandMovesNextRowSignal(test);
    testOneDominantPeakPerBand(test);
    testNegativeBiasGivesLeftDominance(test);
    testPositiveBiasGivesRightDominance(test);
    testZeroBiasGivesEqualBeams(test);

    return test.result();
}
