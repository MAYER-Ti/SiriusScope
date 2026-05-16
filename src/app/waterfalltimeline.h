#ifndef WATERFALLTIMELINE_H
#define WATERFALLTIMELINE_H

#include "waterfallstorage.h"

#include <QString>
#include <QVector>

struct WaterfallTimeTick
{
    int y = 0;
    qint64 utcMs = 0;
    QString label;
    bool major = false;
};

class WaterfallTimelineViewport
{
public:
    enum class Mode
    {
        Live,
        History
    };

    explicit WaterfallTimelineViewport(int visibleRowCount = 360,
                                       qint64 rowPeriodMs = 1000);

    WaterfallSessionId sessionId() const noexcept { return m_sessionId; }
    Mode mode() const noexcept { return m_mode; }
    bool liveMode() const noexcept { return m_mode == Mode::Live; }
    bool hasSession() const noexcept { return m_sessionId.isValid(); }

    qint64 topUtcMs() const noexcept { return m_topUtcMs; }
    qint64 bottomUtcMs() const noexcept;
    qint64 rowPeriodMs() const noexcept { return m_rowPeriodMs; }
    int visibleRowCount() const noexcept { return m_visibleRowCount; }

    void setVisibleRowCount(int visibleRowCount);
    void setRowPeriodMs(qint64 rowPeriodMs);
    void setMode(Mode mode) noexcept { m_mode = mode; }
    void clear();

    bool jumpToLive(qint64 newestUtcMs);
    bool scrollRows(int rowsOlderPositive);
    bool switchToSession(const WaterfallSessionId& id,
                         qint64 newestUtcMs,
                         qint64 rowPeriodMs,
                         Mode mode);

private:
    WaterfallSessionId m_sessionId;
    qint64 m_topUtcMs = 0;
    qint64 m_rowPeriodMs = 1000;
    int m_visibleRowCount = 360;
    Mode m_mode = Mode::History;
};

class WaterfallTimelineMapper
{
public:
    WaterfallTimelineMapper(qint64 topUtcMs,
                            qint64 rowPeriodMs,
                            int visibleRowCount,
                            int pixelHeight);

    int yForUtcMs(qint64 utcMs) const;
    qint64 utcMsForY(int y) const;
    int rowIndexForUtcMs(qint64 utcMs) const;
    qint64 utcMsForRowIndex(int rowIndex) const;

    QVector<WaterfallTimeTick> buildTimeTicks() const;

private:
    qint64 m_topUtcMs = 0;
    qint64 m_rowPeriodMs = 1000;
    int m_visibleRowCount = 1;
    int m_pixelHeight = 0;
};

#endif // WATERFALLTIMELINE_H
