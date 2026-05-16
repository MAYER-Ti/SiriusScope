#include "waterfallhistorymodel.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr int kMinTickDistancePx = 14;

struct RowSegment
{
    int firstRow = 0;
    int lastRow = 0;
};

struct TickCandidate
{
    qint64 utcMs = 0;
    int y = 0;
};

bool rowEarlier(const WaterfallRow& lhs, const WaterfallRow& rhs)
{
    if (lhs.utcMs != rhs.utcMs) {
        return lhs.utcMs < rhs.utcMs;
    }
    return lhs.firstSampleIndex < rhs.firstSampleIndex;
}

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

qint64 medianPositiveRowDeltaMs(const QVector<WaterfallRow>& rows)
{
    QVector<qint64> deltas;
    deltas.reserve(std::max<qsizetype>(0, rows.size() - 1));

    for (int row = 0; row + 1 < rows.size(); ++row) {
        const qint64 delta = rows.at(row).utcMs - rows.at(row + 1).utcMs;
        if (delta > 0) {
            deltas.push_back(delta);
        }
    }

    if (deltas.isEmpty()) {
        return 1000;
    }

    std::sort(deltas.begin(), deltas.end());
    return deltas.at(deltas.size() / 2);
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

QVector<RowSegment> continuousSegments(const QVector<WaterfallRow>& rows, qint64 typicalDeltaMs)
{
    QVector<RowSegment> segments;
    if (rows.isEmpty()) {
        return segments;
    }

    const qint64 gapThresholdMs = std::max<qint64>(5000, typicalDeltaMs * 4);
    int segmentStart = 0;

    for (int row = 0; row + 1 < rows.size(); ++row) {
        const qint64 delta = rows.at(row).utcMs - rows.at(row + 1).utcMs;
        if (delta > gapThresholdMs) {
            segments.push_back(RowSegment{segmentStart, row});
            segmentStart = row + 1;
        }
    }

    segments.push_back(RowSegment{segmentStart, static_cast<int>(rows.size() - 1)});
    return segments;
}

int yForRowPosition(double rowPosition, int pixelHeight, int visibleRowCount)
{
    if (pixelHeight <= 0 || visibleRowCount <= 0) {
        return 0;
    }

    const int maxPixelY = std::max(0, pixelHeight - 1);
    const double y = rowPosition / static_cast<double>(visibleRowCount)
        * static_cast<double>(pixelHeight);
    return std::clamp(static_cast<int>(std::lround(y)), 0, maxPixelY);
}

int yForUtcMs(qint64 utcMs,
              const QVector<WaterfallRow>& rows,
              int pixelHeight,
              int visibleRowCount)
{
    if (rows.size() <= 1 || pixelHeight <= 0 || visibleRowCount <= 0) {
        return 0;
    }

    const int lastRow = rows.size() - 1;
    if (utcMs >= rows.first().utcMs) {
        return 0;
    }
    if (utcMs <= rows.last().utcMs) {
        return yForRowPosition(lastRow, pixelHeight, visibleRowCount);
    }

    for (int row = 0; row < lastRow; ++row) {
        const qint64 upperUtcMs = rows.at(row).utcMs;
        const qint64 lowerUtcMs = rows.at(row + 1).utcMs;
        if (upperUtcMs == lowerUtcMs) {
            continue;
        }
        if (upperUtcMs >= utcMs && utcMs >= lowerUtcMs) {
            const double rowFraction =
                static_cast<double>(upperUtcMs - utcMs)
                / static_cast<double>(upperUtcMs - lowerUtcMs);
            const double rowPosition = static_cast<double>(row)
                + std::clamp(rowFraction, 0.0, 1.0);
            return yForRowPosition(rowPosition, pixelHeight, visibleRowCount);
        }
    }

    const double totalSpanMs = static_cast<double>(rows.first().utcMs - rows.last().utcMs);
    if (totalSpanMs <= 0.0) {
        return 0;
    }

    const double ratio = static_cast<double>(rows.first().utcMs - utcMs) / totalSpanMs;
    return yForRowPosition(ratio * static_cast<double>(lastRow), pixelHeight, visibleRowCount);
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

    ticks.push_back(WaterfallTimeTick{y, label, major});
}

QVector<WaterfallTimeTick> rowSpacedTimeTicks(const QVector<WaterfallRow>& rows,
                                              int pixelHeight,
                                              int visibleRowCount)
{
    QVector<WaterfallTimeTick> ticks;
    if (rows.isEmpty() || pixelHeight <= 0) {
        return ticks;
    }

    const int maxPixelY = std::max(0, pixelHeight - 1);
    if (rows.size() == 1 || rows.first().utcMs == rows.last().utcMs) {
        bool major = false;
        ticks.push_back(WaterfallTimeTick{
            0,
            localTimeLabel(rows.first().utcMs, QDate{}, false, &major),
            major
        });
        return ticks;
    }

    const int desiredTickCount = std::clamp(pixelHeight / 80 + 1, 2, 8);
    const int tickCount = std::min<int>(rows.size(), desiredTickCount);
    ticks.reserve(tickCount);

    QDate previousDate;
    bool hasPreviousDate = false;
    int previousY = std::numeric_limits<int>::min();

    for (int i = 0; i < tickCount; ++i) {
        const double ratio = tickCount == 1 ? 0.0 : static_cast<double>(i) / (tickCount - 1);
        const int rowIndex = std::clamp<int>(
            static_cast<int>(std::lround(ratio * static_cast<double>(rows.size() - 1))),
            0,
            rows.size() - 1);
        const int y = std::clamp(
            yForRowPosition(rowIndex, pixelHeight, visibleRowCount),
            0,
            maxPixelY);

        if (!ticks.isEmpty() && std::abs(y - previousY) < kMinTickDistancePx) {
            continue;
        }

        appendTick(ticks, rows.at(rowIndex).utcMs, y, previousDate, hasPreviousDate);
        previousY = y;
    }

    return ticks;
}

} // namespace

WaterfallHistoryModel::WaterfallHistoryModel(int visibleRowCount)
    : m_visibleRowCount(std::max(1, visibleRowCount))
{
}

void WaterfallHistoryModel::setVisibleRowCount(int visibleRowCount)
{
    m_visibleRowCount = std::max(1, visibleRowCount);
    clampWindow();
}

void WaterfallHistoryModel::setRows(QVector<WaterfallRow> rows)
{
    m_rows = std::move(rows);
    sortRows();

    if (m_mode == Mode::Live) {
        m_windowEndExclusive = m_rows.size();
    }
    clampWindow();
}

void WaterfallHistoryModel::appendLiveRow(const WaterfallRow& row)
{
    m_rows.push_back(row);
    if (m_rows.size() > 1 && rowEarlier(m_rows.back(), m_rows.at(m_rows.size() - 2))) {
        sortRows();
    }

    if (m_mode == Mode::Live) {
        m_windowEndExclusive = m_rows.size();
    }
    clampWindow();
}

bool WaterfallHistoryModel::scrollRows(int rowsOlderPositive)
{
    if (m_rows.isEmpty() || rowsOlderPositive == 0) {
        return false;
    }

    const qsizetype oldEnd = m_windowEndExclusive;
    if (rowsOlderPositive > 0) {
        m_windowEndExclusive =
            std::max(minimumWindowEnd(), m_windowEndExclusive - rowsOlderPositive);
    } else {
        m_windowEndExclusive =
            std::min<qsizetype>(m_rows.size(), m_windowEndExclusive - rowsOlderPositive);
    }

    m_mode = m_windowEndExclusive >= m_rows.size() ? Mode::Live : Mode::History;
    return oldEnd != m_windowEndExclusive;
}

bool WaterfallHistoryModel::jumpToLive()
{
    const bool changed = m_mode != Mode::Live || m_windowEndExclusive != m_rows.size();
    m_mode = Mode::Live;
    m_windowEndExclusive = m_rows.size();
    clampWindow();
    return changed;
}

QVector<WaterfallRow> WaterfallHistoryModel::visibleRows() const
{
    QVector<WaterfallRow> result;
    if (m_rows.isEmpty() || m_windowEndExclusive <= 0) {
        return result;
    }

    const qsizetype end = std::clamp<qsizetype>(m_windowEndExclusive, 0, m_rows.size());
    const qsizetype start = std::max<qsizetype>(0, end - m_visibleRowCount);
    result.reserve(end - start);

    for (qsizetype i = end; i > start; --i) {
        result.push_back(m_rows.at(i - 1));
    }
    return result;
}

QVector<WaterfallTimeTick> WaterfallHistoryModel::visibleTimeTicks(int pixelHeight) const
{
    const QVector<WaterfallRow> rows = visibleRows();
    if (rows.isEmpty() || pixelHeight <= 0) {
        return {};
    }

    if (rows.size() == 1 || rows.first().utcMs == rows.last().utcMs) {
        bool major = false;
        return {
            WaterfallTimeTick{
            0,
            localTimeLabel(rows.first().utcMs, QDate{}, false, &major),
            major
            }
        };
    }

    const qint64 typicalDeltaMs = medianPositiveRowDeltaMs(rows);
    const int targetTickCount = std::clamp(pixelHeight / 80 + 1, 2, 8);
    const qint64 targetStepMs = std::max<qint64>(
        1000,
        typicalDeltaMs * std::max(1, m_visibleRowCount - 1)
            / std::max(1, targetTickCount - 1));
    const qint64 stepMs = niceTimeStepMs(targetStepMs);

    QVector<TickCandidate> candidates;
    candidates.reserve(targetTickCount + 2);

    const QVector<RowSegment> segments = continuousSegments(rows, typicalDeltaMs);
    for (const RowSegment& segment : segments) {
        const qint64 newestUtcMs = rows.at(segment.firstRow).utcMs;
        const qint64 oldestUtcMs = rows.at(segment.lastRow).utcMs;
        bool segmentHasTick = false;

        for (qint64 tickUtcMs = alignUp(oldestUtcMs, stepMs);
             tickUtcMs <= newestUtcMs;
             tickUtcMs += stepMs) {
            candidates.push_back(TickCandidate{
                tickUtcMs,
                yForUtcMs(tickUtcMs, rows, pixelHeight, m_visibleRowCount)
            });
            segmentHasTick = true;
            if (tickUtcMs > std::numeric_limits<qint64>::max() - stepMs) {
                break;
            }
        }

        if (!segmentHasTick) {
            candidates.push_back(TickCandidate{
                newestUtcMs,
                yForUtcMs(newestUtcMs, rows, pixelHeight, m_visibleRowCount)
            });
        }
    }

    if (candidates.isEmpty()) {
        return rowSpacedTimeTicks(rows, pixelHeight, m_visibleRowCount);
    }

    std::sort(candidates.begin(), candidates.end(), [](const TickCandidate& lhs,
                                                       const TickCandidate& rhs) {
        if (lhs.y != rhs.y) {
            return lhs.y < rhs.y;
        }
        return lhs.utcMs > rhs.utcMs;
    });

    QVector<WaterfallTimeTick> ticks;
    ticks.reserve(candidates.size());
    QDate previousDate;
    bool hasPreviousDate = false;
    int previousY = std::numeric_limits<int>::min();

    for (const TickCandidate& candidate : candidates) {
        if (!ticks.isEmpty() && std::abs(candidate.y - previousY) < kMinTickDistancePx) {
            continue;
        }

        appendTick(ticks, candidate.utcMs, candidate.y, previousDate, hasPreviousDate);
        previousY = candidate.y;
    }

    return ticks.size() >= 2 ? ticks : rowSpacedTimeTicks(rows, pixelHeight, m_visibleRowCount);
}

QString WaterfallHistoryModel::currentUtcText() const
{
    const QVector<WaterfallRow> rows = visibleRows();
    if (rows.isEmpty()) {
        return QStringLiteral("UTC --:--:--");
    }

    return QDateTime::fromMSecsSinceEpoch(rows.first().utcMs, QTimeZone::UTC)
        .toString(QStringLiteral("dd.MM.yyyy HH:mm:ss 'UTC'"));
}

void WaterfallHistoryModel::sortRows()
{
    std::sort(m_rows.begin(), m_rows.end(), rowEarlier);
}

void WaterfallHistoryModel::clampWindow()
{
    if (m_rows.isEmpty()) {
        m_windowEndExclusive = 0;
        m_mode = Mode::Live;
        return;
    }

    m_windowEndExclusive =
        std::clamp<qsizetype>(m_windowEndExclusive, minimumWindowEnd(), m_rows.size());
    m_mode = m_windowEndExclusive >= m_rows.size() ? Mode::Live : Mode::History;
}

qsizetype WaterfallHistoryModel::minimumWindowEnd() const noexcept
{
    return std::min<qsizetype>(m_visibleRowCount, m_rows.size());
}
