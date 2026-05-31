#include "app/scanbandresultaggregator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
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

core::BearingResult makeBearingResult(
    int bandIndex,
    std::vector<std::int64_t> frequenciesHz,
    double bearingAzimuthDeg,
    std::optional<double> quality = 0.84,
    std::uint64_t sampleIndex = 12,
    std::int64_t utcNs = 1'700'000'000'000'000'000LL)
{
    auto created = core::BearingResult::create(sampleIndex,
                                               utcNs,
                                               bandIndex,
                                               bearingAzimuthDeg,
                                               frequenciesHz,
                                               quality);
    return *created.value();
}

const core::BearingResult* findByBand(const std::vector<core::BearingResult>& results,
                                      int bandIndex)
{
    const auto it = std::find_if(results.begin(), results.end(), [bandIndex](const auto& result) {
        return result.bandIndex == bandIndex;
    });
    return it == results.end() ? nullptr : &(*it);
}

bool near(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

bool nearZeroAzimuth(double azimuthDeg, double tolerance)
{
    return azimuthDeg <= tolerance || azimuthDeg >= 360.0 - tolerance;
}

void testSingleResultRemainsSingleResult(TestRunner& test)
{
    const auto results = app::ScanBandResultAggregator::aggregateByBand(
        {makeBearingResult(0, {1'000'000'000LL}, 30.0)});

    test.require(results.size() == 1, "single result remains single result");
    test.require(results.front().bandIndex == 0, "single result preserves band index");
    test.require(results.front().frequenciesHz == std::vector<std::int64_t>{1'000'000'000LL},
                 "single result preserves frequencies");
    test.require(near(results.front().bearingAzimuthDeg, 30.0, 0.001),
                 "single result preserves bearing");
}

void testMultipleResultsSameBandBecomeOneResult(TestRunner& test)
{
    const auto results = app::ScanBandResultAggregator::aggregateByBand({
        makeBearingResult(0, {1'000'000'000LL}, 30.0),
        makeBearingResult(0, {1'005'000'000LL}, 32.0),
        makeBearingResult(0, {1'010'000'000LL}, 31.0),
    });

    test.require(results.size() == 1, "same band results are aggregated");
    test.require(results.front().bandIndex == 0, "same band aggregation preserves band index");
    test.require(results.front().frequenciesHz
                     == std::vector<std::int64_t>{
                         1'000'000'000LL,
                         1'005'000'000LL,
                         1'010'000'000LL,
                     },
                 "same band aggregation joins frequencies");
    test.require(near(results.front().bearingAzimuthDeg, 31.0, 0.25),
                 "same band aggregation averages bearing circularly");
}

void testDifferentBandsStaySeparate(TestRunner& test)
{
    const auto results = app::ScanBandResultAggregator::aggregateByBand({
        makeBearingResult(1, {2'000'000'000LL}, 120.0),
        makeBearingResult(0, {1'000'000'000LL}, 30.0),
    });

    test.require(results.size() == 2, "different bands stay separate");
    test.require(results[0].bandIndex == 0, "aggregated bands are sorted by band index");
    test.require(results[1].bandIndex == 1, "second aggregated band is preserved");
    test.require(findByBand(results, 0) != nullptr, "band 0 exists");
    test.require(findByBand(results, 1) != nullptr, "band 1 exists");
}

void testDuplicateFrequenciesAreUniqueSorted(TestRunner& test)
{
    const auto results = app::ScanBandResultAggregator::aggregateByBand({
        makeBearingResult(0, {1'010'000'000LL}, 31.0),
        makeBearingResult(0, {1'000'000'000LL}, 30.0),
        makeBearingResult(0, {1'010'000'000LL}, 32.0),
    });

    test.require(results.size() == 1, "duplicate frequency aggregation returns one row");
    test.require(results.front().frequenciesHz
                     == std::vector<std::int64_t>{1'000'000'000LL, 1'010'000'000LL},
                 "duplicate frequencies are unique and sorted");
}

void testCircularMeanAcrossZeroBoundary(TestRunner& test)
{
    const auto results = app::ScanBandResultAggregator::aggregateByBand({
        makeBearingResult(0, {1'000'000'000LL}, 359.0),
        makeBearingResult(0, {1'005'000'000LL}, 1.0),
    });

    test.require(results.size() == 1, "circular mean returns one row");
    test.require(nearZeroAzimuth(results.front().bearingAzimuthDeg, 1.0),
                 "359 and 1 degrees aggregate near zero");
    test.require(std::abs(results.front().bearingAzimuthDeg - 180.0) > 100.0,
                 "359 and 1 degrees do not aggregate near 180");
}

void testMaxQualityIsUsed(TestRunner& test)
{
    const auto results = app::ScanBandResultAggregator::aggregateByBand({
        makeBearingResult(0, {1'000'000'000LL}, 30.0, 0.2),
        makeBearingResult(0, {1'005'000'000LL}, 32.0, 0.8),
        makeBearingResult(0, {1'010'000'000LL}, 31.0, 0.5),
    });

    test.require(results.size() == 1, "quality aggregation returns one row");
    test.require(results.front().quality && *results.front().quality == 0.8,
                 "max valid quality is used");
}

} // namespace

int main()
{
    TestRunner test;

    testSingleResultRemainsSingleResult(test);
    testMultipleResultsSameBandBecomeOneResult(test);
    testDifferentBandsStaySeparate(test);
    testDuplicateFrequenciesAreUniqueSorted(test);
    testCircularMeanAcrossZeroBoundary(test);
    testMaxQualityIsUsed(test);

    return test.result();
}
