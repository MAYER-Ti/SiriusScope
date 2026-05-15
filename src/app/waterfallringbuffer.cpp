/*!
 *  \file waterfallringbuffer.cpp
 *  \brief Реализация кольцевого буфера строк Waterfall.
 */
#include "waterfallringbuffer.h"

#include "waterfallstorage.h"

#include <algorithm>
#include <cstring>
#include <QMutexLocker>

WaterfallRingBuffer::WaterfallRingBuffer(int nbins,
                                         int height,
                                         double globalMinHz,
                                         double globalMaxHz,
                                         QObject *parent)
    : QObject(parent)
    , m_data(std::max(0, nbins) * std::max(0, height))
    , m_nbins(std::max(0, nbins))
    , m_height(std::max(0, height))
    , m_globalMinHz(globalMinHz)
    , m_globalMaxHz(globalMaxHz)
{
}

const WaterfallBeamBin *WaterfallRingBuffer::linePtr(int row) const noexcept
{
    if (row < 0 || row >= m_height || m_nbins <= 0) {
        return nullptr;
    }
    return m_data.constData() + static_cast<qsizetype>(row) * m_nbins;
}

void WaterfallRingBuffer::pushLine(const WaterfallBeamBin *line, int nbins, uint64_t generationId)
{
    if (!line || nbins != m_nbins || m_height <= 0) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);

        if (m_height > 1) {
            std::memmove(m_data.data() + m_nbins,
                         m_data.constData(),
                         static_cast<size_t>(m_nbins) * static_cast<size_t>(m_height - 1)
                             * sizeof(WaterfallBeamBin));
        }

        WaterfallBeamBin *dest = m_data.data();
        std::memcpy(dest, line, static_cast<size_t>(m_nbins) * sizeof(WaterfallBeamBin));

        const int populatedRows = std::min(m_height, m_populatedRows.load(std::memory_order_relaxed) + 1);
        m_populatedRows.store(populatedRows, std::memory_order_release);
        m_generationId.store(generationId, std::memory_order_release);
        m_writeIndex.fetch_add(1, std::memory_order_release);
    }

    emit contentsChanged();
}

void WaterfallRingBuffer::replaceRows(const QVector<WaterfallRow>& rows, uint64_t generationId)
{
    {
        QMutexLocker locker(&m_mutex);

        std::fill(m_data.begin(), m_data.end(), WaterfallBeamBin{});

        const int rowCount = std::min(m_height, static_cast<int>(rows.size()));
        for (int row = 0; row < rowCount; ++row) {
            const auto& source = rows.at(row).bins;
            if (source.isEmpty()) {
                continue;
            }

            const int binsToCopy = std::min(m_nbins, static_cast<int>(source.size()));
            WaterfallBeamBin *dest = m_data.data() + static_cast<qsizetype>(row) * m_nbins;
            std::memcpy(dest, source.constData(), static_cast<size_t>(binsToCopy) * sizeof(WaterfallBeamBin));
        }

        m_populatedRows.store(rowCount, std::memory_order_release);
        m_generationId.store(generationId, std::memory_order_release);
        m_writeIndex.fetch_add(1, std::memory_order_release);
    }

    emit contentsChanged();
}

bool WaterfallRingBuffer::copyLine(int row, WaterfallBeamBin *destination, int nbins) const
{
    if (!destination || nbins != m_nbins || row < 0 || row >= m_height || m_nbins <= 0) {
        return false;
    }

    QMutexLocker locker(&m_mutex);
    const WaterfallBeamBin *src = m_data.constData() + static_cast<qsizetype>(row) * m_nbins;
    std::memcpy(destination, src, static_cast<size_t>(m_nbins) * sizeof(WaterfallBeamBin));
    return true;
}
