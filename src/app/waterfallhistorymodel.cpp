#include "waterfallhistorymodel.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>

namespace {

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
    QVector<WaterfallTimeTick> ticks;
    const QVector<WaterfallRow> rows = visibleRows();
    if (rows.isEmpty() || pixelHeight <= 0) {
        return ticks;
    }

    const int tickCount = std::min<int>(
        rows.size(),
        std::clamp(pixelHeight / 80 + 1, 2, 8));
    ticks.reserve(tickCount);

    QDate previousDate;
    bool hasPreviousDate = false;

    for (int i = 0; i < tickCount; ++i) {
        const double ratio = tickCount == 1 ? 0.0 : static_cast<double>(i) / (tickCount - 1);
        const int rowIndex = std::clamp<int>(
            qRound(ratio * static_cast<double>(rows.size() - 1)),
            0,
            rows.size() - 1);

        bool major = false;
        const QString label =
            localTimeLabel(rows.at(rowIndex).utcMs, previousDate, hasPreviousDate, &major);

        const QDateTime localTime =
            QDateTime::fromMSecsSinceEpoch(rows.at(rowIndex).utcMs,
                                           QTimeZone::systemTimeZone());
        previousDate = localTime.date();
        hasPreviousDate = true;

        ticks.push_back(WaterfallTimeTick{
            qRound(ratio * static_cast<double>(pixelHeight)),
            label,
            major
        });
    }

    return ticks;
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
