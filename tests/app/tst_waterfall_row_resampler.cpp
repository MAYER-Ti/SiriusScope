#include "app/waterfallrowresampler.h"
#include "app/waterfallstorage.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

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

WaterfallRow makeRow(double minHz, double maxHz, QVector<WaterfallBeamBin> bins)
{
    WaterfallRow row;
    row.viewMinHz = minHz;
    row.viewMaxHz = maxHz;
    row.bins = std::move(bins);
    return row;
}

bool allZero(const QVector<WaterfallBeamBin>& values)
{
    return std::all_of(values.cbegin(), values.cend(), [](const WaterfallBeamBin& value) {
        return value.left == 0 && value.right == 0;
    });
}

void testMatchingRange(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {{0, 10}, {20, 18}, {40, 36}});
    const auto result = WaterfallRowResampler::resample(row, 100.0, 200.0, 3);

    test.require(result == row.bins, "matching range preserves two-beam bins");
}

void testZoomIn(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0,
                                     200.0,
                                     {{0, 80}, {20, 60}, {40, 40}, {60, 20}, {80, 0}});
    const auto result = WaterfallRowResampler::resample(row, 125.0, 175.0, 3);

    test.require(result.size() == 3, "zoom-in result has requested size");
    test.require(result.at(0) == WaterfallBeamBin{20, 60},
                 "zoom-in interpolates left edge for both beams");
    test.require(result.at(1) == WaterfallBeamBin{40, 40},
                 "zoom-in interpolates middle for both beams");
    test.require(result.at(2) == WaterfallBeamBin{60, 20},
                 "zoom-in interpolates right edge for both beams");
}

void testTargetWiderThanSource(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {{10, 30}, {20, 40}, {30, 50}});
    const auto result = WaterfallRowResampler::resample(row, 50.0, 250.0, 5);

    test.require(result.size() == 5, "wider target result has requested size");
    test.require(result.first() == WaterfallBeamBin{}, "wider target has zero left edge");
    test.require(result.last() == WaterfallBeamBin{}, "wider target has zero right edge");
    test.require(result.at(2).left > 0 && result.at(2).right > 0,
                 "wider target keeps source data in the middle");
}

void testNoIntersection(TestRunner& test)
{
    const WaterfallRow row = makeRow(100.0, 200.0, {{10, 20}, {20, 30}, {30, 40}});
    const auto result = WaterfallRowResampler::resample(row, 300.0, 400.0, 6);

    test.require(result.size() == 6, "non-intersection result has requested size");
    test.require(allZero(result), "non-intersection result is zero");
}

void testInvalidRangesAndSingleBin(TestRunner& test)
{
    const WaterfallRow invalidSource = makeRow(200.0, 100.0, {{10, 20}, {20, 30}});
    const auto invalidSourceResult =
        WaterfallRowResampler::resample(invalidSource, 100.0, 200.0, 4);
    test.require(allZero(invalidSourceResult), "invalid source range returns zero result");

    const WaterfallRow row = makeRow(100.0, 200.0, {{10, 20}, {20, 30}, {30, 40}});
    const auto invalidTargetResult = WaterfallRowResampler::resample(row, 200.0, 100.0, 4);
    test.require(allZero(invalidTargetResult), "invalid target range returns zero result");

    const auto singleBinResult = WaterfallRowResampler::resample(row, 100.0, 200.0, 1);
    test.require(singleBinResult.size() == 1 && singleBinResult.first() == WaterfallBeamBin{10, 20},
                 "single output bin samples target minimum");
}

} // namespace

int main()
{
    TestRunner test;

    testMatchingRange(test);
    testZoomIn(test);
    testTargetWiderThanSource(test);
    testNoIntersection(test);
    testInvalidRangesAndSingleBin(test);

    return test.result();
}
