#include "waterfallstorage.h"

#include <algorithm>
#include <limits>

namespace {

bool rowEarlier(const WaterfallRow& lhs, const WaterfallRow& rhs)
{
    if (lhs.utcMs != rhs.utcMs) {
        return lhs.utcMs < rhs.utcMs;
    }
    return lhs.firstSampleIndex < rhs.firstSampleIndex;
}

} // namespace

QVector<WaterfallRow> InMemoryWaterfallStorage::loadRows(qint64 fromUtcMs,
                                                         qint64 toUtcMs,
                                                         int maxRows)
{
    QVector<WaterfallRow> result;
    result.reserve(m_rows.size());

    const qint64 minUtc = std::min(fromUtcMs, toUtcMs);
    const qint64 maxUtc = std::max(fromUtcMs, toUtcMs);

    for (const auto& row : m_rows) {
        if (row.utcMs >= minUtc && row.utcMs <= maxUtc) {
            result.push_back(row);
        }
    }

    std::sort(result.begin(), result.end(), rowEarlier);

    if (maxRows > 0 && result.size() > maxRows) {
        result.erase(result.begin(), result.begin() + (result.size() - maxRows));
    }

    return result;
}

void InMemoryWaterfallStorage::appendRow(const WaterfallRow& row)
{
    const auto insertAt = std::lower_bound(m_rows.begin(), m_rows.end(), row, rowEarlier);
    m_rows.insert(insertAt, row);
}

void InMemoryWaterfallStorage::appendRows(const QVector<WaterfallRow>& rows)
{
    m_rows.reserve(m_rows.size() + rows.size());
    for (const auto& row : rows) {
        m_rows.push_back(row);
    }
    std::sort(m_rows.begin(), m_rows.end(), rowEarlier);
}
