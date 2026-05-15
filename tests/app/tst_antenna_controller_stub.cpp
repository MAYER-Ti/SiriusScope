#include "app/antennacontrollerstub.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

void processFor(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

bool near(double actual, double expected, double tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

bool containsNear(const std::vector<double>& values, double expected, double tolerance)
{
    for (const auto value : values) {
        if (near(value, expected, tolerance)) {
            return true;
        }
    }
    return false;
}

bool hasBlindZoneValue(const std::vector<double>& values)
{
    for (const auto value : values) {
        if (value > 170.0 && value < 190.0) {
            return true;
        }
    }
    return false;
}

void recordAzimuths(AntennaControllerStub& controller, std::vector<double>& values)
{
    QObject::connect(&controller,
                     &AntennaControllerStub::azimuthDegChanged,
                     [&values](double azimuthDeg) { values.push_back(azimuthDeg); });
}

void testScanDoesNotTeleportAndFirstTickIsBounded(TestRunner& test)
{
    AntennaControllerStub controller;
    std::vector<double> values;
    recordAzimuths(controller, values);

    controller.setAzimuthDeg(230.0);
    values.clear();
    controller.scan(220.0, 150.0, 20);

    test.require(near(controller.azimuthDeg(), 230.0, 0.001),
                 "scan does not teleport to a sector boundary immediately");

    processFor(150);

    test.require(controller.azimuthDeg() < 230.0, "first scan tick moves toward nearest boundary");
    test.require(controller.azimuthDeg() > 225.0,
                 "first scan tick is bounded by speed and elapsed time");
    test.require(!hasBlindZoneValue(values), "bounded scan tick stays outside blind zone");
}

void testScanChoosesLeftBoundaryWhenItIsCloser(TestRunner& test)
{
    AntennaControllerStub controller;
    std::vector<double> values;
    int stopped = 0;
    recordAzimuths(controller, values);
    QObject::connect(&controller, &AntennaControllerStub::stopped, [&stopped]() { ++stopped; });

    controller.setAzimuthDeg(230.0);
    values.clear();
    controller.scan(220.0, 150.0, 1000);
    processFor(650);

    test.require(containsNear(values, 220.0, 0.5), "scan reaches closer left boundary first");
    test.require(near(controller.azimuthDeg(), 150.0, 0.5),
                 "scan from left boundary finishes at right boundary");
    test.require(stopped == 1, "scan completion emits stopped once");
    test.require(!hasBlindZoneValue(values), "left-to-right scan avoids blind zone");
}

void testScanChoosesRightBoundaryWhenItIsCloser(TestRunner& test)
{
    AntennaControllerStub controller;
    std::vector<double> values;
    int stopped = 0;
    recordAzimuths(controller, values);
    QObject::connect(&controller, &AntennaControllerStub::stopped, [&stopped]() { ++stopped; });

    controller.setAzimuthDeg(140.0);
    values.clear();
    controller.scan(220.0, 150.0, 1000);
    processFor(650);

    test.require(containsNear(values, 150.0, 0.5), "scan reaches closer right boundary first");
    test.require(near(controller.azimuthDeg(), 220.0, 0.5),
                 "scan from right boundary finishes at left boundary");
    test.require(stopped == 1, "reverse scan completion emits stopped once");
    test.require(!hasBlindZoneValue(values), "right-to-left scan avoids blind zone");
}

void testMaxSectorCanScanBothDirections(TestRunner& test)
{
    {
        AntennaControllerStub controller;
        std::vector<double> values;
        recordAzimuths(controller, values);

        controller.setAzimuthDeg(200.0);
        values.clear();
        controller.scan(190.0, 170.0, 1000);
        processFor(700);

        test.require(containsNear(values, 190.0, 0.5),
                     "max sector reaches 190 boundary when it is closer");
        test.require(near(controller.azimuthDeg(), 170.0, 0.5),
                     "max sector 190-to-170 direction finishes at 170");
        test.require(!hasBlindZoneValue(values), "max sector 190-to-170 avoids blind zone");
    }

    {
        AntennaControllerStub controller;
        std::vector<double> values;
        recordAzimuths(controller, values);

        controller.setAzimuthDeg(160.0);
        values.clear();
        controller.scan(190.0, 170.0, 1000);
        processFor(700);

        test.require(containsNear(values, 170.0, 0.5),
                     "max sector reaches 170 boundary when it is closer");
        test.require(near(controller.azimuthDeg(), 190.0, 0.5),
                     "max sector 170-to-190 direction finishes at 190");
        test.require(!hasBlindZoneValue(values), "max sector 170-to-190 avoids blind zone");
    }
}

void testManualDriveStopsAtBlindZoneBoundaries(TestRunner& test)
{
    {
        AntennaControllerStub controller;
        std::vector<double> values;
        int stopped = 0;
        recordAzimuths(controller, values);
        QObject::connect(&controller, &AntennaControllerStub::stopped, [&stopped]() { ++stopped; });

        controller.setAzimuthDeg(168.0);
        values.clear();
        controller.driveRight(50);
        processFor(180);

        test.require(near(controller.azimuthDeg(), 170.0, 0.001),
                     "driveRight stops at 170 blind-zone boundary");
        test.require(stopped == 1, "driveRight boundary stop emits stopped");
        test.require(!hasBlindZoneValue(values), "driveRight does not enter blind zone");
    }

    {
        AntennaControllerStub controller;
        std::vector<double> values;
        int stopped = 0;
        recordAzimuths(controller, values);
        QObject::connect(&controller, &AntennaControllerStub::stopped, [&stopped]() { ++stopped; });

        controller.setAzimuthDeg(192.0);
        values.clear();
        controller.driveLeft(50);
        processFor(180);

        test.require(near(controller.azimuthDeg(), 190.0, 0.001),
                     "driveLeft stops at 190 blind-zone boundary");
        test.require(stopped == 1, "driveLeft boundary stop emits stopped");
        test.require(!hasBlindZoneValue(values), "driveLeft does not enter blind zone");
    }
}

void testStopHaltsMotion(TestRunner& test)
{
    AntennaControllerStub controller;
    int commanded = 0;
    int stopped = 0;

    QObject::connect(&controller, &AntennaControllerStub::driveRightCommanded, [&commanded](int speed) {
        if (speed == 60) {
            ++commanded;
        }
    });
    QObject::connect(&controller, &AntennaControllerStub::stopped, [&stopped]() { ++stopped; });

    controller.setAzimuthDeg(100.0);
    controller.driveRight(60);
    processFor(160);

    test.require(commanded == 1, "driveRight emits command signal");
    test.require(controller.azimuthDeg() > 100.0, "driveRight moves before stop");

    controller.stop();
    const auto stoppedAt = controller.azimuthDeg();
    processFor(220);

    test.require(stopped == 1, "explicit stop emits stopped signal");
    test.require(near(controller.azimuthDeg(), stoppedAt, 0.5), "stop halts motion");
}

void testInvalidCommandsRejected(TestRunner& test)
{
    AntennaControllerStub controller;
    int rejected = 0;
    int commanded = 0;

    QObject::connect(&controller, &AntennaControllerStub::commandRejected, [&rejected](const QString&) {
        ++rejected;
    });
    QObject::connect(&controller, &AntennaControllerStub::driveRightCommanded, [&commanded](int) {
        ++commanded;
    });

    controller.setAzimuthDeg(50.0);
    controller.driveRight(0);
    processFor(120);

    test.require(rejected == 1, "non-positive manual speed is rejected");
    test.require(commanded == 0, "invalid manual speed does not emit drive command");
    test.require(near(controller.azimuthDeg(), 50.0, 0.001), "invalid manual speed does not move");

    controller.scan(175.0, 220.0, 10);
    controller.scan(220.0, 185.0, 10);
    controller.scan(220.0, 150.0, 0);
    controller.setAzimuthDeg(175.0);
    controller.scan(220.0, 150.0, 10);
    processFor(120);

    test.require(rejected == 5, "scan rejects blind-zone angles, invalid speed, and blind current angle");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testScanDoesNotTeleportAndFirstTickIsBounded(test);
    testScanChoosesLeftBoundaryWhenItIsCloser(test);
    testScanChoosesRightBoundaryWhenItIsCloser(test);
    testMaxSectorCanScanBothDirections(test);
    testManualDriveStopsAtBlindZoneBoundaries(test);
    testStopHaltsMotion(test);
    testInvalidCommandsRejected(test);

    return test.result();
}
