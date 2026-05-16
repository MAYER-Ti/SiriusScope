#ifndef WATERFALLSTORAGE_H
#define WATERFALLSTORAGE_H

#include <optional>

#include <QMutex>
#include <QString>
#include <QVector>

#include <cstdint>

inline constexpr uint16_t kWaterfallRenderAmplitudeMin = 1;
inline constexpr uint16_t kWaterfallRenderAmplitudeMax = 127;

struct WaterfallBeamBin
{
    uint16_t left = 0;
    uint16_t right = 0;

    friend bool operator==(const WaterfallBeamBin& lhs, const WaterfallBeamBin& rhs) noexcept
    {
        return lhs.left == rhs.left && lhs.right == rhs.right;
    }
};

struct WaterfallSessionId
{
    QString value;

    bool isValid() const noexcept { return !value.isEmpty(); }

    friend bool operator==(const WaterfallSessionId& lhs,
                           const WaterfallSessionId& rhs) noexcept
    {
        return lhs.value == rhs.value;
    }

    friend bool operator!=(const WaterfallSessionId& lhs,
                           const WaterfallSessionId& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

struct WaterfallSessionMetadata
{
    WaterfallSessionId id;
    qint64 startUtcMs = 0;
    qint64 endUtcMs = 0;
    qint64 rowPeriodMs = 1000;
    int binCount = 1024;
    int bandCount = 5;
    int beamCount = 2;
    QString sourceName;
    bool closed = false;
};

enum class WaterfallSessionMode
{
    Inactive,
    Active,
    History
};

struct WaterfallRow
{
    WaterfallSessionId sessionId;
    qint64 utcMs = 0;
    quint64 firstSampleIndex = 0;
    quint64 lastSampleIndex = 0;
    double viewMinHz = 0.0;
    double viewMaxHz = 0.0;
    QVector<WaterfallBeamBin> bins;
};

struct WaterfallRowSlot
{
    bool occupied = false;
    WaterfallRow row;
};

class IWaterfallStorage
{
public:
    virtual ~IWaterfallStorage() = default;

    virtual QVector<WaterfallRow> loadRows(qint64 fromUtcMs,
                                           qint64 toUtcMs,
                                           int maxRows) = 0;

    virtual void appendRow(const WaterfallRow& row) = 0;
};

class InMemoryWaterfallStorage final : public IWaterfallStorage
{
public:
    QVector<WaterfallRow> loadRows(qint64 fromUtcMs,
                                   qint64 toUtcMs,
                                   int maxRows) override;

    void appendRow(const WaterfallRow& row) override;
    void appendRows(const QVector<WaterfallRow>& rows);

private:
    QVector<WaterfallRow> m_rows;
};

class IWaterfallSessionStorage
{
public:
    virtual ~IWaterfallSessionStorage() = default;

    virtual QVector<WaterfallSessionMetadata> listSessions() const = 0;
    virtual std::optional<WaterfallSessionMetadata> session(
        const WaterfallSessionId& id) const = 0;
    virtual std::optional<WaterfallSessionMetadata> latestSession() const = 0;
    virtual std::optional<WaterfallSessionMetadata> previousSession(
        const WaterfallSessionId& id) const = 0;
    virtual std::optional<WaterfallSessionMetadata> nextSession(
        const WaterfallSessionId& id) const = 0;

    virtual WaterfallSessionMetadata startSession(WaterfallSessionMetadata metadata) = 0;
    virtual bool closeSession(const WaterfallSessionId& id, qint64 endUtcMs) = 0;
    virtual void appendRow(const WaterfallSessionId& id, const WaterfallRow& row) = 0;
    virtual QVector<WaterfallRow> loadRows(const WaterfallSessionId& id,
                                           qint64 fromUtcMs,
                                           qint64 toUtcMs,
                                           int maxRows) const = 0;
    virtual int rowCount(const WaterfallSessionId& id) const = 0;
};

class InMemoryWaterfallSessionStorage final : public IWaterfallSessionStorage
{
public:
    QVector<WaterfallSessionMetadata> listSessions() const override;
    std::optional<WaterfallSessionMetadata> session(
        const WaterfallSessionId& id) const override;
    std::optional<WaterfallSessionMetadata> latestSession() const override;
    std::optional<WaterfallSessionMetadata> previousSession(
        const WaterfallSessionId& id) const override;
    std::optional<WaterfallSessionMetadata> nextSession(
        const WaterfallSessionId& id) const override;

    WaterfallSessionMetadata startSession(WaterfallSessionMetadata metadata) override;
    bool closeSession(const WaterfallSessionId& id, qint64 endUtcMs) override;
    void appendRow(const WaterfallSessionId& id, const WaterfallRow& row) override;
    QVector<WaterfallRow> loadRows(const WaterfallSessionId& id,
                                   qint64 fromUtcMs,
                                   qint64 toUtcMs,
                                   int maxRows) const override;
    int rowCount(const WaterfallSessionId& id) const override;

private:
    struct SessionData
    {
        WaterfallSessionMetadata metadata;
        QVector<WaterfallRow> rows;
    };

    qsizetype findSessionIndex(const WaterfallSessionId& id) const;
    void sortSessions();

    mutable QMutex m_mutex;
    QVector<SessionData> m_sessions;
};

#endif // WATERFALLSTORAGE_H
