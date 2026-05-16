#include "waterfalltimeline.h"

#include <QDate>
#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr int kMinTickDistancePx = 14;

QString localTimeLabel(qint64 utcMs, const QDate& previousDate, bool hasPrevious, bool* major)
{
    const QDateTime localTime =
        QDateTime::fromMSecsSinceEpoch(utcMs, QTimeZone::systemTimeZone());
    const QDate localDate = localTime.date();
    const bool firstDifferentDay = !hasPrevious && localDate != QDate::currentDate();
    const bool crossedDay = hasPrevious && localDate != previousDate;
    const bool includeDate = firstDifferentDay || crossedDay;

    if (major) {
        *major = !hasPrevious || includeDate;
    }

    return includeDate
        ? localTime.toString(QStringLiteral("dd.MM HH:mm:ss"))
        : localTime.toString(QStringLiteral("HH:mm:ss"));
}

qint64 niceTimeStepMs(qint64 targetMs)
{
    constexpr std::array<qint64, 23> kStepsMs = {
        1000,
        2000,
        5000,
        10 * 1000,
        15 * 1000,
        30 * 1000,
        60 * 1000,
        2 * 60 * 1000,
        5 * 60 * 1000,
        10 * 60 * 1000,
        15 * 60 * 1000,
        30 * 60 * 1000,
        60 * 60 * 1000,
        2 * 60 * 60 * 1000,
        3 * 60 * 60 * 1000,
        6 * 60 * 60 * 1000,
        12 * 60 * 60 * 1000,
        24 * 60 * 60 * 1000,
        2 * 24 * 60 * 60 * 1000,
        7 * 24 * 60 * 60 * 1000,
        14 * 24 * 60 * 60 * 1000,
        30LL * 24 * 60 * 60 * 1000,
        90LL * 24 * 60 * 60 * 1000
    };

    targetMs = std::max<qint64>(1000, targetMs);
    qint64 bestStep = kStepsMs.front();
    double bestDistance = std::abs(static_cast<double>(bestStep - targetMs));

    for (const qint64 step : kStepsMs) {
        const double distance = std::abs(static_cast<double>(step - targetMs));
        if (distance < bestDistance) {
            bestStep = step;
            bestDistance = distance;
        }
    }

    return bestStep;
}

qint64 alignUp(qint64 value, qint64 step)
{
    if (step <= 0) {
        return value;
    }

    const qint64 remainder = value % step;
    if (remainder == 0) {
        return value;
    }

    return value > 0
        ? value + (step - remainder)
        : value - remainder;
}

void appendTick(QVector<WaterfallTimeTick>& ticks,
                qint64 utcMs,
                int y,
                QDate& previousDate,
                bool& hasPreviousDate)
{
    bool major = false;
    const QString label = localTimeLabel(utcMs, previousDate, hasPreviousDate, &major);

    const QDateTime localTime =
        QDateTime::fromMSecsSinceEpoch(utcMs, QTimeZone::systemTimeZone());
    previousDate = localTime.date();
    hasPreviousDate = true;

    ticks.push_back(WaterfallTimeTick{y, utcMs, label, major});
}

} // namespace

WaterfallTimelineViewport::WaterfallTimelineViewport(int visibleRowCount,
                                                     qint64 rowPeriodMs)
    : m_rowPeriodMs(std::max<qint64>(1, rowPeriodMs))
    , m_visibleRowCount(std::max(1, visibleRowCount))
{
}

qint64 WaterfallTimelineViewport::bottomUtcMs() const noexcept
{
    if (m_topUtcMs <= 0) {
        return 0;
    }
    return m_topUtcMs
        - static_cast<qint64>(std::max(0, m_visibleRowCount - 1)) * m_rowPeriodMs;
}

void WaterfallTimelineViewport::setVisibleRowCount(int visibleRowCount)
{
    m_visibleRowCount = std::max(1, visibleRowCount);
}

void WaterfallTimelineViewport::setRowPeriodMs(qint64 rowPeriodMs)
{
    m_rowPeriodMs = std::max<qint64>(1, rowPeriodMs);
}

void WaterfallTimelineViewport::clear()
{
    m_sessionId = {};
    m_topUtcMs = 0;
    m_mode = Mode::History;
}

bool WaterfallTimelineViewport::jumpToLive(qint64 newestUtcMs)
{
    newestUtcMs = std::max<qint64>(0, newestUtcMs);
    const bool changed = m_mode != Mode::Live || m_topUtcMs != newestUtcMs;
    m_mode = Mode::Live;
    m_topUtcMs = newestUtcMs;
    return changed;
}

bool WaterfallTimelineViewport::scrollRows(int rowsOlderPositive)
{
    if (rowsOlderPositive == 0 || !hasSession()) {
        return false;
    }

    const qint64 oldTop = m_topUtcMs;
    m_topUtcMs -= static_cast<qint64>(rowsOlderPositive) * m_rowPeriodMs;
    m_mode = Mode::History;
    return m_topUtcMs != oldTop;
}

bool WaterfallTimelineViewport::switchToSession(const WaterfallSessionId& id,
                                                qint64 newestUtcMs,
                                                qint64 rowPeriodMs,
                                                Mode mode)
{
    newestUtcMs = std::max<qint64>(0, newestUtcMs);
    rowPeriodMs = std::max<qint64>(1, rowPeriodMs);
    const bool changed = m_sessionId != id
        || m_topUtcMs != newestUtcMs
        || m_rowPeriodMs != rowPeriodMs
        || m_mode != mode;

    m_sessionId = id;
    m_topUtcMs = newestUtcMs;
    m_rowPeriodMs = rowPeriodMs;
    m_mode = mode;
    return changed;
}

WaterfallTimelineMapper::WaterfallTimelineMapper(qint64 topUtcMs,
                                                 qint64 rowPeriodMs,
                                                 int visibleRowCount,
                                                 int pixelHeight)
    : m_topUtcMs(topUtcMs)
    , m_rowPeriodMs(std::max<qint64>(1, rowPeriodMs))
    , m_visibleRowCount(std::max(1, visibleRowCount))
    , m_pixelHeight(std::max(0, pixelHeight))
{
}

int WaterfallTimelineMapper::yForUtcMs(qint64 utcMs) const
{
    if (m_pixelHeight <= 0) {
        return 0;
    }

    const double rowPosition =
        static_cast<double>(m_topUtcMs - utcMs) / static_cast<double>(m_rowPeriodMs);
    const double y = rowPosition / static_cast<double>(m_visibleRowCount)
        * static_cast<double>(m_pixelHeight);
    return std::clamp(static_cast<int>(std::lround(y)), 0, std::max(0, m_pixelHeight - 1));
}

qint64 WaterfallTimelineMapper::utcMsForY(int y) const
{
    if (m_pixelHeight <= 0) {
        return m_topUtcMs;
    }

    const double clampedY = std::clamp(y, 0, std::max(0, m_pixelHeight - 1));
    const double rowPosition =
        clampedY / static_cast<double>(m_pixelHeight)
        * static_cast<double>(m_visibleRowCount);
    return m_topUtcMs - static_cast<qint64>(std::llround(rowPosition * m_rowPeriodMs));
}

int WaterfallTimelineMapper::rowIndexForUtcMs(qint64 utcMs) const
{
    const double rowPosition =
        static_cast<double>(m_topUtcMs - utcMs) / static_cast<double>(m_rowPeriodMs);
    const int rowIndex = static_cast<int>(std::llround(rowPosition));
    return rowIndex >= 0 && rowIndex < m_visibleRowCount ? rowIndex : -1;
}

qint64 WaterfallTimelineMapper::utcMsForRowIndex(int rowIndex) const
{
    return m_topUtcMs - static_cast<qint64>(rowIndex) * m_rowPeriodMs;
}

QVector<WaterfallTimeTick> WaterfallTimelineMapper::buildTimeTicks() const
{
    QVector<WaterfallTimeTick> ticks;
    if (m_topUtcMs <= 0 || m_pixelHeight <= 0 || m_visibleRowCount <= 0) {
        return ticks;
    }

    const qint64 spanMs =
        static_cast<qint64>(std::max(1, m_visibleRowCount - 1)) * m_rowPeriodMs;
    const qint64 bottomUtcMs = m_topUtcMs - spanMs;
    const int targetTickCount = std::clamp(m_pixelHeight / 80 + 1, 2, 8);
    const qint64 targetStepMs =
        std::max<qint64>(m_rowPeriodMs, spanMs / std::max(1, targetTickCount - 1));
    const qint64 stepMs = std::max(m_rowPeriodMs, niceTimeStepMs(targetStepMs));

    QVector<qint64> candidates;
    candidates.reserve(targetTickCount + 3);
    candidates.push_back(m_topUtcMs);

    for (qint64 tickUtcMs = alignUp(bottomUtcMs, stepMs);
         tickUtcMs <= m_topUtcMs;
         tickUtcMs += stepMs) {
        candidates.push_back(tickUtcMs);
        if (tickUtcMs > std::numeric_limits<qint64>::max() - stepMs) {
            break;
        }
    }

    candidates.push_back(bottomUtcMs);
    std::sort(candidates.begin(), candidates.end(), std::greater<qint64>());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    QDate previousDate;
    bool hasPreviousDate = false;
    int previousY = std::numeric_limits<int>::min();

    for (const qint64 utcMs : candidates) {
        const int y = yForUtcMs(utcMs);
        if (!ticks.isEmpty() && std::abs(y - previousY) < kMinTickDistancePx) {
            continue;
        }

        appendTick(ticks, utcMs, y, previousDate, hasPreviousDate);
        previousY = y;
    }

    return ticks;
}
