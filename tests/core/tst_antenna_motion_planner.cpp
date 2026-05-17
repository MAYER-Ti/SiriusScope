#include "core/antenna_motion_planner.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
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

bool near(double lhs, double rhs, double tolerance = 0.001)
{
    return std::abs(lhs - rhs) <= tolerance;
}

void testValidSectorAccepted(TestRunner& test)
{
    const auto planned = core::AntennaMotionPlanner::planSectorScan(10.0, 60.0);

    test.require(planned.hasValue(), "valid sector is accepted");
    test.require(planned && near(planned.value()->startAzimuthDeg, 10.0),
                 "valid sector keeps start azimuth");
    test.require(planned && near(planned.value()->endAzimuthDeg, 60.0),
                 "valid sector keeps end azimuth");
    test.require(planned && near(planned.value()->spanDeg, 50.0),
                 "valid sector stores span");
}

void testInvalidWidthsRejected(TestRunner& test)
{
    const auto zeroWidth = core::AntennaMotionPlanner::planSectorScan(10.0, 10.0);
    test.require(!zeroWidth, "zero-width sector is rejected");

    core::ScanMotionOptions options;
    options.minSectorDeg = 5.0;
    const auto tooSmall = core::AntennaMotionPlanner::planSectorScan(10.0, 12.0, options);
    test.require(!tooSmall, "too-small sector is rejected");
}

void testRawAzimuthValidation(TestRunner& test)
{
    test.require(!core::AntennaMotionPlanner::planSectorScan(-1.0, 20.0),
                 "negative azimuth is rejected");
    test.require(!core::AntennaMotionPlanner::planSectorScan(10.0, 360.0),
                 "azimuth 360 is rejected");
}

void testBlindZoneEndpointRejected(TestRunner& test)
{
    test.require(!core::AntennaMotionPlanner::planSectorScan(180.0, 220.0),
                 "left endpoint inside blind zone is rejected");
    test.require(!core::AntennaMotionPlanner::planSectorScan(120.0, 180.0),
                 "right endpoint inside blind zone is rejected");
    test.require(core::AntennaMotionPlanner::planSectorScan(170.0, 190.0).hasValue(),
                 "blind zone boundaries are valid endpoints");
}

void testCrossingNorthAccepted(TestRunner& test)
{
    const auto planned = core::AntennaMotionPlanner::planSectorScan(350.0, 20.0);

    test.require(planned.hasValue(), "sector crossing north is accepted");
    test.require(planned && planned.value()->crossesNorthDeg,
                 "sector crossing north is marked");
    test.require(planned && near(planned.value()->spanDeg, 30.0),
                 "sector crossing north keeps safe span");
}

void testSafeMappingReversible(TestRunner& test)
{
    const std::vector<double> azimuths{0.0, 10.0, 170.0, 190.0, 200.0, 359.0};
    for (const auto azimuth : azimuths) {
        const auto safeCoord = core::AntennaMotionPlanner::toSafeCoord(azimuth);
        const auto restored = core::AntennaMotionPlanner::fromSafeCoord(safeCoord);
        test.require(near(restored, core::AntennaMotionPlanner::normalizeAzimuth(azimuth)),
                     "safe coordinate mapping is reversible outside blind zone");
    }
}

void testBlindZoneCrossingRequestPlannedSafely(TestRunner& test)
{
    const auto planned = core::AntennaMotionPlanner::planSectorScan(150.0, 200.0);

    test.require(planned.hasValue(), "sector around blind zone is planned safely");
    test.require(planned && near(planned.value()->startAzimuthDeg, 200.0),
                 "safe path starts after blind zone");
    test.require(planned && near(planned.value()->endAzimuthDeg, 150.0),
                 "safe path ends before blind zone");
    test.require(planned && planned.value()->requestedSector.isWrapAround(),
                 "safe path uses wrap-around sector");
}

void testCurrentAzimuthChoosesNearestStart(TestRunner& test)
{
    const auto nearLeft =
        core::AntennaMotionPlanner::planSectorScanFromCurrentAzimuth(40.0, 100.0, 35.0);
    test.require(nearLeft.hasValue(), "current-aware sector near left is accepted");
    test.require(nearLeft && near(nearLeft.value()->startAzimuthDeg, 40.0),
                 "current azimuth near left chooses left start");
    test.require(nearLeft && near(nearLeft.value()->endAzimuthDeg, 100.0),
                 "left-start scan ends at right side");
    test.require(nearLeft
                     && nearLeft.value()->direction == core::ScanDirection::IncreasingSafeCoord,
                 "left-start scan increases safe coordinate");

    const auto nearRight =
        core::AntennaMotionPlanner::planSectorScanFromCurrentAzimuth(40.0, 100.0, 110.0);
    test.require(nearRight.hasValue(), "current-aware sector near right is accepted");
    test.require(nearRight && near(nearRight.value()->startAzimuthDeg, 100.0),
                 "current azimuth near right chooses right start");
    test.require(nearRight && near(nearRight.value()->endAzimuthDeg, 40.0),
                 "right-start scan ends at left side");
    test.require(nearRight
                     && nearRight.value()->direction == core::ScanDirection::DecreasingSafeCoord,
                 "right-start scan decreases safe coordinate");
}

void testReverseScanKeepsPositiveSpan(TestRunner& test)
{
    const auto planned =
        core::AntennaMotionPlanner::planSectorScanFromCurrentAzimuth(40.0, 100.0, 110.0);

    test.require(planned.hasValue(), "reverse scan path is planned");
    test.require(planned && planned.value()->spanDeg > 0.0,
                 "reverse scan keeps positive span");

    if (planned) {
        const auto currentCoord =
            core::AntennaMotionPlanner::toSafeCoord(70.0);
        const auto traveled = planned.value()->safeStartCoordDeg - currentCoord;
        const auto progress = traveled / planned.value()->spanDeg;
        test.require(progress > 0.0 && progress < 1.0,
                     "reverse scan progress can be calculated");
    }
}

} // namespace

int main()
{
    TestRunner test;

    testValidSectorAccepted(test);
    testInvalidWidthsRejected(test);
    testRawAzimuthValidation(test);
    testBlindZoneEndpointRejected(test);
    testCrossingNorthAccepted(test);
    testSafeMappingReversible(test);
    testBlindZoneCrossingRequestPlannedSafely(test);
    testCurrentAzimuthChoosesNearestStart(test);
    testReverseScanKeepsPositiveSpan(test);

    return test.result();
}
