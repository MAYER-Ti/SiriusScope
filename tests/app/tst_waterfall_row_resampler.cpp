#include "app/waterfallrowresampler.h"
#include "app/waterfallstorage.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
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

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

WaterfallRow makeRow(double minHz, double maxHz, QVector<uint16_t> bins)
{
    WaterfallRow row;
    row.viewMinHz = minHz;
    row.viewMaxHz = maxHz;
    row.bins = std::move(bins);
    return row;
}

bool allZero(const QVector<uint16_t>& values)
{
    return std::all_of(values.cbegin(), values.cend(), [](uint16_t value) {
        return value == 0;
    });
}

void testMatchingRange(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {0, 1000, 2000, 3000, 4000});
    const auto result = WaterfallRowResampler::resample(row, 100.0, 200.0, 5);

    test.require(result == row.bins, "matching range preserves bins");
}

void testZoomIn(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {0, 1000, 2000, 3000, 4000});
    const auto result = WaterfallRowResampler::resample(row, 125.0, 175.0, 3);

    test.require(result.size() == 3, "zoom-in result has requested size");
    test.require(result.at(0) == 1000, "zoom-in starts at central source section");
    test.require(result.at(1) == 2000, "zoom-in interpolates middle source section");
    test.require(result.at(2) == 3000, "zoom-in ends at central source section");
}

void testTargetWiderThanSource(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {100, 200, 300});
    const auto result = WaterfallRowResampler::resample(row, 50.0, 250.0, 5);

    test.require(result.size() == 5, "wider target result has requested size");
    test.require(result.first() == 0, "wider target has zero left edge");
    test.require(result.last() == 0, "wider target has zero right edge");
    test.require(result.at(2) > 0, "wider target keeps source data in the middle");
}

void testViewportShiftedRight(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {0, 1000, 2000, 3000, 4000});
    const auto result = WaterfallRowResampler::resample(row, 150.0, 250.0, 5);

    test.require(result.at(0) == 2000, "shifted viewport starts with overlapping source data");
    test.require(result.at(1) == 3000, "shifted viewport keeps overlapping source data");
    test.require(result.at(2) == 4000, "shifted viewport includes source right edge");
    test.require(result.at(3) == 0 && result.at(4) == 0,
                 "shifted viewport fills non-overlapping right side with zero");
}

void testNoIntersection(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {100, 200, 300});
    const auto result = WaterfallRowResampler::resample(row, 300.0, 400.0, 6);

    test.require(result.size() == 6, "non-intersection result has requested size");
    test.require(allZero(result), "non-intersection result is zero");
}

void testEmptyBins(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {});
    const auto result = WaterfallRowResampler::resample(row, 100.0, 200.0, 4);

    test.require(result.size() == 4, "empty source result has requested size");
    test.require(allZero(result), "empty source result is zero");
}

void testInvalidRangesAndSingleBin(TestRunner& test)
{
    const WaterfallRow invalidSource = makeRow(200.0, 100.0, {100, 200, 300});
    const auto invalidSourceResult =
        WaterfallRowResampler::resample(invalidSource, 100.0, 200.0, 4);
    test.require(allZero(invalidSourceResult), "invalid source range returns zero result");

    const WaterfallRow row = makeRow(100.0, 200.0, {100, 200, 300});
    const auto invalidTargetResult = WaterfallRowResampler::resample(row, 200.0, 100.0, 4);
    test.require(allZero(invalidTargetResult), "invalid target range returns zero result");

    const auto singleBinResult = WaterfallRowResampler::resample(row, 100.0, 200.0, 1);
    test.require(singleBinResult.size() == 1 && singleBinResult.first() == 100,
                 "single output bin samples target minimum");
}

} // namespace

int main()
{
    TestRunner test;

    testMatchingRange(test);
    testZoomIn(test);
    testTargetWiderThanSource(test);
    testViewportShiftedRight(test);
    testNoIntersection(test);
    testEmptyBins(test);
    testInvalidRangesAndSingleBin(test);

    return test.result();
}
