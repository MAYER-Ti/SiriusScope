#include "app/waterfalltimeline.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimeZone>

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

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

qint64 localMs(const QDate& date, const QTime& time)
{
    return QDateTime(date, time, QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
}

void testMapperConvertsTimeRowsAndPixels(TestRunner& test)
{
    const qint64 topUtcMs = localMs(QDate::currentDate(), QTime(12, 0, 0));
    WaterfallTimelineMapper mapper(topUtcMs, 1000, 10, 100);

    test.require(mapper.rowIndexForUtcMs(topUtcMs) == 0,
                 "top UTC maps to row slot 0");
    test.require(mapper.rowIndexForUtcMs(topUtcMs - 3000) == 3,
                 "older UTC maps to the matching lower row slot");
    test.require(mapper.utcMsForRowIndex(4) == topUtcMs - 4000,
                 "row slot maps back to UTC using the row period");
    test.require(mapper.yForUtcMs(topUtcMs - 5000) == 50,
                 "Y coordinate uses fixed visibleRowCount scale");
    test.require(mapper.utcMsForY(50) == topUtcMs - 5000,
                 "Y coordinate maps back to the same timeline position");
}

void testViewportScrollDirection(TestRunner& test)
{
    const qint64 topUtcMs = localMs(QDate::currentDate(), QTime(12, 0, 0));
    WaterfallTimelineViewport viewport(10, 1000);
    viewport.switchToSession(WaterfallSessionId{QStringLiteral("s1")},
                             topUtcMs,
                             1000,
                             WaterfallTimelineViewport::Mode::Live);

    viewport.scrollRows(2);
    test.require(viewport.topUtcMs() == topUtcMs - 2000,
                 "positive scroll steps move viewport toward older history");
    test.require(!viewport.liveMode(), "scrolling older leaves live mode");

    viewport.scrollRows(-1);
    test.require(viewport.topUtcMs() == topUtcMs - 1000,
                 "negative scroll steps move viewport toward live");
}

void testTimeTicksAreBoundedAndShareY(TestRunner& test)
{
    const qint64 topUtcMs = localMs(QDate::currentDate(), QTime(12, 0, 0));
    WaterfallTimelineMapper mapper(topUtcMs, 1000, 360, 320);
    const auto ticks = mapper.buildTimeTicks();

    test.require(ticks.size() >= 2, "timeline mapper returns multiple ticks");
    test.require(ticks.size() <= 8, "timeline mapper keeps tick count bounded");
    test.require(std::all_of(ticks.cbegin(), ticks.cend(), [](const WaterfallTimeTick& tick) {
        return tick.y >= 0 && tick.y < 320 && tick.utcMs > 0 && !tick.label.isEmpty();
    }), "ticks have stable Y coordinates, UTC values, and labels");
}

void testTimeTicksMoveWhenTopTimeMoves(TestRunner& test)
{
    const qint64 topUtcMs = localMs(QDate::currentDate(), QTime(12, 0, 0));
    WaterfallTimelineMapper first(topUtcMs, 1000, 60, 300);
    WaterfallTimelineMapper second(topUtcMs + 1000, 1000, 60, 300);

    const auto firstTicks = first.buildTimeTicks();
    const auto secondTicks = second.buildTimeTicks();
    const auto findTickY = [](const QVector<WaterfallTimeTick>& ticks, qint64 utcMs) {
        for (const auto& tick : ticks) {
            if (tick.utcMs == utcMs) {
                return tick.y;
            }
        }
        return -1;
    };

    const qint64 trackedUtcMs = topUtcMs - 30'000;
    const int firstY = findTickY(firstTicks, trackedUtcMs);
    const int secondY = findTickY(secondTicks, trackedUtcMs);
    test.require(firstY >= 0 && secondY > firstY,
                 "same timeline tick moves down when a newer top row appears");
}

void testSessionStorageLoadsRowsBySessionAndTime(TestRunner& test)
{
    InMemoryWaterfallSessionStorage storage;
    WaterfallSessionMetadata first;
    first.id = WaterfallSessionId{QStringLiteral("first")};
    first.startUtcMs = 1000;
    first.endUtcMs = 1000;
    first.rowPeriodMs = 1000;

    WaterfallSessionMetadata second = first;
    second.id = WaterfallSessionId{QStringLiteral("second")};
    second.startUtcMs = 10'000;
    second.endUtcMs = 10'000;

    storage.startSession(first);
    storage.startSession(second);

    WaterfallRow row;
    row.utcMs = 1000;
    storage.appendRow(first.id, row);
    row.utcMs = 2000;
    storage.appendRow(first.id, row);
    row.utcMs = 10'000;
    storage.appendRow(second.id, row);

    const auto firstRows = storage.loadRows(first.id, 0, 3000, 10);
    const auto secondRows = storage.loadRows(second.id, 0, 3000, 10);
    const auto previous = storage.previousSession(second.id);

    test.require(firstRows.size() == 2, "storage loads rows for the requested session");
    test.require(secondRows.isEmpty(), "storage does not leak rows from another session");
    test.require(previous && previous->id == first.id,
                 "previousSession returns the earlier session by metadata time");
}

} // namespace

int main()
{
    TestRunner test;

    testMapperConvertsTimeRowsAndPixels(test);
    testViewportScrollDirection(test);
    testTimeTicksAreBoundedAndShareY(test);
    testTimeTicksMoveWhenTopTimeMoves(test);
    testSessionStorageLoadsRowsBySessionAndTime(test);

    return test.result();
}
