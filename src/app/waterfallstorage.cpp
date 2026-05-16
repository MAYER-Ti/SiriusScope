#include "waterfallstorage.h"

#include <algorithm>
#include <utility>

namespace {

bool rowEarlier(const WaterfallRow& lhs, const WaterfallRow& rhs)
{
    if (lhs.utcMs != rhs.utcMs) {
        return lhs.utcMs < rhs.utcMs;
    }
    return lhs.firstSampleIndex < rhs.firstSampleIndex;
}

bool sessionEarlier(const WaterfallSessionMetadata& lhs,
                    const WaterfallSessionMetadata& rhs)
{
    if (lhs.startUtcMs != rhs.startUtcMs) {
        return lhs.startUtcMs < rhs.startUtcMs;
    }
    return lhs.id.value < rhs.id.value;
}

WaterfallSessionId generatedSessionId(qint64 startUtcMs, int sequence)
{
    return WaterfallSessionId{
        QStringLiteral("session-%1-%2")
            .arg(startUtcMs)
            .arg(sequence, 3, 10, QLatin1Char('0'))
    };
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

QVector<WaterfallSessionMetadata> InMemoryWaterfallSessionStorage::listSessions() const
{
    QMutexLocker lock(&m_mutex);
    QVector<WaterfallSessionMetadata> sessions;
    sessions.reserve(m_sessions.size());
    for (const auto& sessionData : m_sessions) {
        sessions.push_back(sessionData.metadata);
    }
    std::sort(sessions.begin(), sessions.end(), sessionEarlier);
    return sessions;
}

std::optional<WaterfallSessionMetadata> InMemoryWaterfallSessionStorage::session(
    const WaterfallSessionId& id) const
{
    QMutexLocker lock(&m_mutex);
    const qsizetype index = findSessionIndex(id);
    if (index < 0) {
        return std::nullopt;
    }
    return m_sessions.at(index).metadata;
}

std::optional<WaterfallSessionMetadata> InMemoryWaterfallSessionStorage::latestSession() const
{
    QMutexLocker lock(&m_mutex);
    if (m_sessions.isEmpty()) {
        return std::nullopt;
    }

    return std::max_element(m_sessions.cbegin(), m_sessions.cend(), [](const auto& lhs,
                                                                       const auto& rhs) {
        return sessionEarlier(lhs.metadata, rhs.metadata);
    })->metadata;
}

std::optional<WaterfallSessionMetadata> InMemoryWaterfallSessionStorage::previousSession(
    const WaterfallSessionId& id) const
{
    QMutexLocker lock(&m_mutex);
    const qsizetype currentIndex = findSessionIndex(id);
    if (currentIndex < 0) {
        if (m_sessions.isEmpty()) {
            return std::nullopt;
        }
        return std::max_element(m_sessions.cbegin(), m_sessions.cend(), [](const auto& lhs,
                                                                           const auto& rhs) {
            return sessionEarlier(lhs.metadata, rhs.metadata);
        })->metadata;
    }

    const auto current = m_sessions.at(currentIndex).metadata;
    std::optional<WaterfallSessionMetadata> previous;
    for (const auto& sessionData : m_sessions) {
        const auto& candidate = sessionData.metadata;
        if (candidate.id == id) {
            continue;
        }
        if (!sessionEarlier(candidate, current)) {
            continue;
        }
        if (!previous || sessionEarlier(*previous, candidate)) {
            previous = candidate;
        }
    }
    return previous;
}

std::optional<WaterfallSessionMetadata> InMemoryWaterfallSessionStorage::nextSession(
    const WaterfallSessionId& id) const
{
    QMutexLocker lock(&m_mutex);
    const qsizetype currentIndex = findSessionIndex(id);
    if (currentIndex < 0) {
        return std::nullopt;
    }

    const auto current = m_sessions.at(currentIndex).metadata;
    std::optional<WaterfallSessionMetadata> next;
    for (const auto& sessionData : m_sessions) {
        const auto& candidate = sessionData.metadata;
        if (candidate.id == id) {
            continue;
        }
        if (!sessionEarlier(current, candidate)) {
            continue;
        }
        if (!next || sessionEarlier(candidate, *next)) {
            next = candidate;
        }
    }
    return next;
}

WaterfallSessionMetadata InMemoryWaterfallSessionStorage::startSession(
    WaterfallSessionMetadata metadata)
{
    QMutexLocker lock(&m_mutex);
    if (!metadata.id.isValid()) {
        metadata.id = generatedSessionId(metadata.startUtcMs, m_sessions.size());
    }
    if (metadata.endUtcMs <= 0) {
        metadata.endUtcMs = metadata.startUtcMs;
    }
    metadata.rowPeriodMs = std::max<qint64>(1, metadata.rowPeriodMs);
    metadata.closed = false;

    const qsizetype existingIndex = findSessionIndex(metadata.id);
    if (existingIndex >= 0) {
        m_sessions[existingIndex].metadata = metadata;
        m_sessions[existingIndex].rows.clear();
    } else {
        m_sessions.push_back(SessionData{metadata, {}});
    }
    sortSessions();
    return metadata;
}

bool InMemoryWaterfallSessionStorage::closeSession(const WaterfallSessionId& id,
                                                   qint64 endUtcMs)
{
    QMutexLocker lock(&m_mutex);
    const qsizetype index = findSessionIndex(id);
    if (index < 0) {
        return false;
    }

    auto& metadata = m_sessions[index].metadata;
    metadata.endUtcMs = std::max(metadata.endUtcMs, endUtcMs);
    metadata.closed = true;
    return true;
}

void InMemoryWaterfallSessionStorage::appendRow(const WaterfallSessionId& id,
                                                const WaterfallRow& row)
{
    QMutexLocker lock(&m_mutex);
    const qsizetype index = findSessionIndex(id);
    if (index < 0) {
        return;
    }

    WaterfallRow stored = row;
    stored.sessionId = id;

    auto& sessionData = m_sessions[index];
    const auto insertAt = std::lower_bound(sessionData.rows.begin(),
                                           sessionData.rows.end(),
                                           stored,
                                           rowEarlier);
    sessionData.rows.insert(insertAt, std::move(stored));
    if (sessionData.rows.size() == 1) {
        sessionData.metadata.startUtcMs = sessionData.rows.first().utcMs;
    } else {
        sessionData.metadata.startUtcMs =
            std::min(sessionData.metadata.startUtcMs, sessionData.rows.first().utcMs);
    }
    sessionData.metadata.endUtcMs =
        std::max(sessionData.metadata.endUtcMs, sessionData.rows.last().utcMs);
    sortSessions();
}

QVector<WaterfallRow> InMemoryWaterfallSessionStorage::loadRows(
    const WaterfallSessionId& id,
    qint64 fromUtcMs,
    qint64 toUtcMs,
    int maxRows) const
{
    QMutexLocker lock(&m_mutex);
    QVector<WaterfallRow> result;
    const qsizetype index = findSessionIndex(id);
    if (index < 0) {
        return result;
    }

    const qint64 minUtc = std::min(fromUtcMs, toUtcMs);
    const qint64 maxUtc = std::max(fromUtcMs, toUtcMs);

    for (const auto& row : m_sessions.at(index).rows) {
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

int InMemoryWaterfallSessionStorage::rowCount(const WaterfallSessionId& id) const
{
    QMutexLocker lock(&m_mutex);
    const qsizetype index = findSessionIndex(id);
    return index < 0 ? 0 : static_cast<int>(m_sessions.at(index).rows.size());
}

qsizetype InMemoryWaterfallSessionStorage::findSessionIndex(
    const WaterfallSessionId& id) const
{
    if (!id.isValid()) {
        return -1;
    }

    for (qsizetype i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i).metadata.id == id) {
            return i;
        }
    }
    return -1;
}

void InMemoryWaterfallSessionStorage::sortSessions()
{
    std::sort(m_sessions.begin(), m_sessions.end(), [](const auto& lhs,
                                                       const auto& rhs) {
        return sessionEarlier(lhs.metadata, rhs.metadata);
    });
}
