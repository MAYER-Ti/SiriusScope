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
    /*!
     *  \brief Creates a fixed-size ring buffer for waterfall rows.
     *  \param[in] nbins Number of frequency bins per row.
     *  \param[in] height Number of rows retained in memory.
     *  \param[in] globalMinHz Lower global frequency bound, in hertz.
     *  \param[in] globalMaxHz Upper global frequency bound, in hertz.
     *  \param[in] parent Optional Qt object parent.
     */
    explicit WaterfallRingBuffer(int nbins,
                                 int height,
                                 double globalMinHz = 0.0,
                                 double globalMaxHz = 0.0,
                                 QObject *parent = nullptr);

    /*!
     *  \brief Appends one row and advances the atomic write index.
     *  \param[in] line Pointer to nbins amplitude/color values.
     *  \param[in] nbins Number of values in line.
     *  \param[in] generationId Viewport/data generation identifier for the row.
     */
    void pushLine(const uint16_t *line, int nbins, uint64_t generationId);

    /*!
     *  \brief Returns the next write position as a monotonically increasing index.
     *  \return Atomic write index.
     */
    uint64_t writeIndex() const noexcept
    {
        return m_writeIndex.load(std::memory_order_acquire);
    }
    /*!
     *  \brief Returns the most recent viewport/data generation identifier.
     *  \return Atomic generation id.
     */
    uint64_t generationId() const noexcept
    {
        return m_generationId.load(std::memory_order_acquire);
    }
    /*!
     *  \brief Returns the number of frequency bins per row.
     *  \return Row width in bins.
     */
    int nbins() const noexcept { return m_nbins; }
    /*!
     *  \brief Returns the number of retained rows.
     *  \return Ring-buffer height in rows.
     */
    int height() const noexcept { return m_height; }
    /*!
     *  \brief Returns the lower global frequency bound.
     *  \return Frequency in hertz.
     */
    double globalMinHz() const noexcept { return m_globalMinHz; }
    /*!
     *  \brief Returns the upper global frequency bound.
     *  \return Frequency in hertz.
     */
    double globalMaxHz() const noexcept { return m_globalMaxHz; }

    /*!
     *  \brief Returns a pointer to a retained row by physical row index.
     *  \param[in] row Physical row index inside [0, height()).
     *  \return Pointer to the first row value, or nullptr for an invalid row.
     */
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
