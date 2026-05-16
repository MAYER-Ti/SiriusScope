#ifndef WATERFALLHISTORYMODEL_H
#define WATERFALLHISTORYMODEL_H

#include "waterfalltimeline.h"

#include <QDate>
#include <QString>
#include <QVector>

class WaterfallHistoryModel
{
public:
    enum class Mode
    {
        Live,
        History
    };

    explicit WaterfallHistoryModel(int visibleRowCount = 360);

    int visibleRowCount() const noexcept { return m_visibleRowCount; }
    void setVisibleRowCount(int visibleRowCount);

    Mode mode() const noexcept { return m_mode; }
    bool liveMode() const noexcept { return m_mode == Mode::Live; }

    int rowCount() const noexcept { return m_rows.size(); }
    qsizetype windowEndExclusive() const noexcept { return m_windowEndExclusive; }

    void setRows(QVector<WaterfallRow> rows);
    void appendLiveRow(const WaterfallRow& row);
    bool scrollRows(int rowsOlderPositive);
    bool jumpToLive();

    QVector<WaterfallRow> visibleRows() const;
    QVector<WaterfallTimeTick> visibleTimeTicks(int pixelHeight) const;
    QString currentUtcText() const;

private:
    void sortRows();
    void clampWindow();
    qsizetype minimumWindowEnd() const noexcept;

    QVector<WaterfallRow> m_rows;
    qsizetype m_windowEndExclusive = 0;
    int m_visibleRowCount = 360;
    Mode m_mode = Mode::Live;
};

#endif // WATERFALLHISTORYMODEL_H
