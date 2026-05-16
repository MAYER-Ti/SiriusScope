#ifndef WATERFALLSTORAGE_H
#define WATERFALLSTORAGE_H

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

struct WaterfallRow
{
    qint64 utcMs = 0;
    quint64 firstSampleIndex = 0;
    quint64 lastSampleIndex = 0;
    double viewMinHz = 0.0;
    double viewMaxHz = 0.0;
    QVector<WaterfallBeamBin> bins;
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

#endif // WATERFALLSTORAGE_H
