/*!
 *  \file waterfallringbuffer.h
 *  \brief Ring buffer for waterfall rows, safe for UI reads.
 */
#ifndef WATERFALLRINGBUFFER_H
#define WATERFALLRINGBUFFER_H

#include <QObject>
#include <QVector>

#include <atomic>
#include <cstdint>

/*!
 *  \class WaterfallRingBuffer
 *  \brief Stores waterfall rows in a ring buffer with atomic indices.
 */
class WaterfallRingBuffer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int nbins READ nbins CONSTANT)
    Q_PROPERTY(int height READ height CONSTANT)
    Q_PROPERTY(double globalMinHz READ globalMinHz CONSTANT)
    Q_PROPERTY(double globalMaxHz READ globalMaxHz CONSTANT)

public:
    explicit WaterfallRingBuffer(int nbins,
                                 int height,
                                 double globalMinHz = 0.0,
                                 double globalMaxHz = 0.0,
                                 QObject *parent = nullptr);

    void pushLine(const uint16_t *line, int nbins, uint64_t generationId);

    uint64_t writeIndex() const noexcept
    {
        return m_writeIndex.load(std::memory_order_acquire);
    }
    uint64_t generationId() const noexcept
    {
        return m_generationId.load(std::memory_order_acquire);
    }
    int nbins() const noexcept { return m_nbins; }
    int height() const noexcept { return m_height; }
    double globalMinHz() const noexcept { return m_globalMinHz; }
    double globalMaxHz() const noexcept { return m_globalMaxHz; }

    const uint16_t *linePtr(int row) const noexcept;

private:
    QVector<uint16_t> m_data;
    std::atomic<uint64_t> m_writeIndex{0};
    std::atomic<uint64_t> m_generationId{0};
    int m_nbins = 0;
    int m_height = 0;
    double m_globalMinHz = 0.0;
    double m_globalMaxHz = 0.0;
};

#endif // WATERFALLRINGBUFFER_H
