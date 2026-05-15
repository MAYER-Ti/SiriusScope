#include "app/waterfallhistorymodel.h"
#include "app/waterfallringbuffer.h"

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

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

qint64 localMs(const QDate& date, const QTime& time)
{
    return QDateTime(date, time, QTimeZone::systemTimeZone()).toMSecsSinceEpoch();
}

WaterfallRow makeRow(qint64 utcMs, quint64 sampleIndex)
{
    WaterfallRow row;
    row.utcMs = utcMs;
    row.firstSampleIndex = sampleIndex;
    row.lastSampleIndex = sampleIndex;
    row.viewMinHz = 300e6;
    row.viewMaxHz = 18e9;
    row.bins = QVector<WaterfallBeamBin>(4,
                                         WaterfallBeamBin{static_cast<uint16_t>(sampleIndex + 1),
                                                          static_cast<uint16_t>(sampleIndex + 2)});
    return row;
}

void appendRows(WaterfallHistoryModel& model, int count, qint64 startMs)
{
    for (int i = 0; i < count; ++i) {
        model.appendLiveRow(makeRow(startMs + i * 1000, static_cast<quint64>(i)));
    }
}

int tickYByLabel(const QVector<WaterfallTimeTick>& ticks, const QString& label)
{
    for (const auto& tick : ticks) {
        if (tick.label == label) {
            return tick.y;
        }
    }
    return -1;
}

bool ticksStayInsidePixelRange(const QVector<WaterfallTimeTick>& ticks, int pixelHeight)
{
    return std::all_of(ticks.cbegin(), ticks.cend(), [pixelHeight](const WaterfallTimeTick& tick) {
        return tick.y >= 0 && tick.y < pixelHeight;
    });
}

bool ticksHaveSeparatedY(const QVector<WaterfallTimeTick>& ticks, int minDistance = 14)
{
    for (int i = 1; i < ticks.size(); ++i) {
        if (std::abs(ticks.at(i).y - ticks.at(i - 1).y) < minDistance) {
            return false;
        }
    }
    return true;
}

void testAppendLiveKeepsNewestWindow(TestRunner& test)
{
    WaterfallHistoryModel model(3);
    appendRows(model, 4, localMs(QDate::currentDate(), QTime(10, 0, 0)));

    const auto rows = model.visibleRows();
    test.require(model.liveMode(), "append keeps model in Live mode");
    test.require(rows.size() == 3, "visible window is capped by configured row count");
    test.require(!rows.isEmpty() && rows.first().firstSampleIndex == 3,
                 "newest live row is displayed at the top");
}

void testScrollHistoryAndReturnToLive(TestRunner& test)
{
    WaterfallHistoryModel model(3);
    appendRows(model, 8, localMs(QDate::currentDate(), QTime(10, 0, 0)));

    model.scrollRows(2);
    auto rows = model.visibleRows();
    test.require(!model.liveMode(), "scrolling up switches to History mode");
    test.require(!rows.isEmpty() && rows.first().firstSampleIndex == 5,
                 "scrolling up moves the top row to older data");

    model.appendLiveRow(makeRow(localMs(QDate::currentDate(), QTime(10, 0, 8)), 8));
    rows = model.visibleRows();
    test.require(!model.liveMode(), "appending while in History does not switch to Live");
    test.require(!rows.isEmpty() && rows.first().firstSampleIndex == 5,
                 "history window does not jump to a newly appended row");

    model.scrollRows(-20);
    rows = model.visibleRows();
    test.require(model.liveMode(), "scrolling down to newest data switches to Live");
    test.require(!rows.isEmpty() && rows.first().firstSampleIndex == 8,
                 "live window shows newest appended row after scrolling down");
}

void testJumpToLive(TestRunner& test)
{
    WaterfallHistoryModel model(3);
    appendRows(model, 8, localMs(QDate::currentDate(), QTime(10, 0, 0)));

    model.scrollRows(3);
    const bool changed = model.jumpToLive();

    const auto rows = model.visibleRows();
    test.require(changed, "jumpToLive reports a state change from History");
    test.require(model.liveMode(), "jumpToLive switches to Live mode");
    test.require(!rows.isEmpty() && rows.first().firstSampleIndex == 7,
                 "jumpToLive returns the visible window to the newest row");
}

void testEmptyTimeTicks(TestRunner& test)
{
    WaterfallHistoryModel model(10);
    const auto ticks = model.visibleTimeTicks(320);

    test.require(ticks.isEmpty(), "empty history returns no time ticks");
}

void testSingleVisibleRowReturnsOneTick(TestRunner& test)
{
    WaterfallHistoryModel model(10);
    model.appendLiveRow(makeRow(localMs(QDate::currentDate(), QTime(10, 0, 0)), 0));

    const auto ticks = model.visibleTimeTicks(320);
    test.require(ticks.size() == 1, "single visible row returns one time tick");
    test.require(ticks.isEmpty() || ticks.first().y == 0, "single row tick is anchored at top");
}

void testTimeTicksUseSecondsAndDateTransitions(TestRunner& test)
{
    WaterfallHistoryModel model(6);
    const QDate today = QDate::currentDate();
    const QDate yesterday = today.addDays(-1);

    model.appendLiveRow(makeRow(localMs(yesterday, QTime(23, 59, 58)), 0));
    model.appendLiveRow(makeRow(localMs(yesterday, QTime(23, 59, 59)), 1));
    model.appendLiveRow(makeRow(localMs(today, QTime(0, 0, 0)), 2));
    model.appendLiveRow(makeRow(localMs(today, QTime(0, 0, 1)), 3));
    model.appendLiveRow(makeRow(localMs(today, QTime(0, 0, 2)), 4));
    model.appendLiveRow(makeRow(localMs(today, QTime(0, 0, 3)), 5));

    const auto ticks = model.visibleTimeTicks(400);
    bool hasSecondsLabel = false;
    bool hasDateLabel = false;
    for (const auto& tick : ticks) {
        if (tick.label.contains(QStringLiteral(":"))) {
            hasSecondsLabel = true;
        }
        if (tick.label.contains(yesterday.toString(QStringLiteral("dd.MM")))) {
            hasDateLabel = true;
            test.require(tick.major, "date transition tick is major");
        }
    }

    test.require(hasSecondsLabel, "time ticks include HH:mm:ss labels");
    test.require(hasDateLabel, "time ticks include date when another day is visible");
    test.require(ticksStayInsidePixelRange(ticks, 400), "time ticks stay inside pixel range");
    test.require(ticksHaveSeparatedY(ticks), "time ticks do not overlap vertically");
}

void testTimeTickYCoordinatesMoveWithScrolledWindow(TestRunner& test)
{
    WaterfallHistoryModel model(20);
    const QDate today = QDate::currentDate();
    appendRows(model, 60, localMs(today, QTime(10, 0, 0)));

    const auto liveTicks = model.visibleTimeTicks(320);
    const int liveY = tickYByLabel(liveTicks, QStringLiteral("10:00:55"));

    model.scrollRows(1);
    const auto scrolledTicks = model.visibleTimeTicks(320);
    const int scrolledY = tickYByLabel(scrolledTicks, QStringLiteral("10:00:55"));

    test.require(liveY >= 0, "live window contains the expected aligned time tick");
    test.require(scrolledY >= 0, "scrolled window keeps the same aligned time tick");
    test.require(liveY != scrolledY, "time tick y changes after history scroll");
    test.require(scrolledY < liveY, "scrolling to older rows moves the same time tick upward");
    test.require(ticksStayInsidePixelRange(liveTicks, 320),
                 "live time ticks stay inside pixel range");
    test.require(ticksStayInsidePixelRange(scrolledTicks, 320),
                 "scrolled time ticks stay inside pixel range");
    test.require(ticksHaveSeparatedY(scrolledTicks),
                 "scrolled time ticks keep separated y coordinates");
}

void testManyRowsHaveSeparatedTickY(TestRunner& test)
{
    WaterfallHistoryModel model(40);
    appendRows(model, 40, localMs(QDate::currentDate(), QTime(10, 0, 0)));

    const auto ticks = model.visibleTimeTicks(320);

    test.require(ticks.size() >= 2, "many rows return multiple time ticks");
    test.require(ticksStayInsidePixelRange(ticks, 320),
                 "many-row time ticks stay inside pixel range");
    test.require(ticksHaveSeparatedY(ticks), "many-row time ticks have separated y coordinates");
}

void testOldestWindowKeepsSeparatedTickY(TestRunner& test)
{
    WaterfallHistoryModel model(20);
    appendRows(model, 60, localMs(QDate::currentDate(), QTime(10, 0, 0)));

    model.scrollRows(1000);
    const auto ticks = model.visibleTimeTicks(320);

    test.require(ticks.size() >= 2, "oldest window still returns multiple time ticks");
    test.require(ticksStayInsidePixelRange(ticks, 320),
                 "oldest-window time ticks stay inside pixel range");
    test.require(ticksHaveSeparatedY(ticks),
                 "oldest-window time ticks do not collapse to one line");
}

void testSessionGapDoesNotCollapseTickY(TestRunner& test)
{
    WaterfallHistoryModel model(20);
    const QDate today = QDate::currentDate();
    const QDate yesterday = today.addDays(-1);

    for (int i = 0; i < 8; ++i) {
        model.appendLiveRow(makeRow(localMs(yesterday, QTime(10, 0, i)),
                                    static_cast<quint64>(i)));
    }
    for (int i = 0; i < 12; ++i) {
        model.appendLiveRow(makeRow(localMs(today, QTime(10, 0, i)),
                                    static_cast<quint64>(8 + i)));
    }

    const auto ticks = model.visibleTimeTicks(320);

    test.require(ticks.size() >= 2, "window with session gap returns multiple time ticks");
    test.require(ticksStayInsidePixelRange(ticks, 320),
                 "session-gap time ticks stay inside pixel range");
    test.require(ticksHaveSeparatedY(ticks),
                 "session-gap time ticks do not collapse to one y coordinate");
}

void testCurrentUtcTextUsesTopVisibleRow(TestRunner& test)
{
    WaterfallHistoryModel model(3);
    const QDate today = QDate::currentDate();

    model.appendLiveRow(makeRow(localMs(today, QTime(12, 0, 0)), 0));
    model.appendLiveRow(makeRow(localMs(today, QTime(12, 0, 1)), 1));

    const QString utcText = model.currentUtcText();
    const QString expected =
        QDateTime::fromMSecsSinceEpoch(localMs(today, QTime(12, 0, 1)), QTimeZone::UTC)
            .toString(QStringLiteral("dd.MM.yyyy HH:mm:ss 'UTC'"));

    test.require(utcText == expected, "current UTC text uses the top visible row");
}

void testRingBufferReplaceRowsProvidesNonZeroTopRow(TestRunner& test)
{
    QVector<WaterfallRow> rows;
    rows.push_back(makeRow(localMs(QDate::currentDate(), QTime(12, 0, 2)), 2));
    rows.push_back(makeRow(localMs(QDate::currentDate(), QTime(12, 0, 1)), 1));
    rows.push_back(makeRow(localMs(QDate::currentDate(), QTime(12, 0, 0)), 0));

    WaterfallRingBuffer buffer(4, 3);
    buffer.replaceRows(rows, 42);

    QVector<WaterfallBeamBin> copied(4);
    const bool copiedTopRow = buffer.copyLine(0, copied.data(), copied.size());

    test.require(buffer.populatedRows() == 3, "replaceRows stores populated row count");
    test.require(buffer.generationId() == 42, "replaceRows stores generation id");
    test.require(buffer.writeIndex() == 1, "replaceRows advances write index");
    test.require(copiedTopRow, "top render row can be copied");
    test.require(copied.at(0).left == 3 && copied.at(0).right == 4,
                 "top render row contains newest visible row");
    test.require(std::any_of(copied.cbegin(), copied.cend(), [](const WaterfallBeamBin& bin) {
        return bin.left > 0 || bin.right > 0;
    }),
                 "top render row contains non-zero samples");
}

} // namespace

int main()
{
    TestRunner test;

    testAppendLiveKeepsNewestWindow(test);
    testScrollHistoryAndReturnToLive(test);
    testJumpToLive(test);
    testEmptyTimeTicks(test);
    testSingleVisibleRowReturnsOneTick(test);
    testTimeTicksUseSecondsAndDateTransitions(test);
    testTimeTickYCoordinatesMoveWithScrolledWindow(test);
    testManyRowsHaveSeparatedTickY(test);
    testOldestWindowKeepsSeparatedTickY(test);
    testSessionGapDoesNotCollapseTickY(test);
    testCurrentUtcTextUsesTopVisibleRow(test);
    testRingBufferReplaceRowsProvidesNonZeroTopRow(test);

    return test.result();
}
