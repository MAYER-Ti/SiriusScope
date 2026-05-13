/*!
 *  \file waterfallringbuffer.cpp
 *  \brief Реализация кольцевого буфера строк Waterfall.
 */
#include "waterfallringbuffer.h"

#include <algorithm>
#include <cstring>

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

const uint16_t *WaterfallRingBuffer::linePtr(int row) const noexcept
{
    if (row < 0 || row >= m_height || m_nbins <= 0) {
        return nullptr;
    }
    return m_data.constData() + static_cast<qsizetype>(row) * m_nbins;
}

void WaterfallRingBuffer::pushLine(const uint16_t *line, int nbins, uint64_t generationId)
{
    if (!line || nbins != m_nbins || m_height <= 0) {
        return;
    }

    m_generationId.store(generationId, std::memory_order_release);

    const uint64_t index = m_writeIndex.load(std::memory_order_relaxed);
    const int row = static_cast<int>(index % static_cast<uint64_t>(m_height));
    uint16_t *dest = m_data.data() + static_cast<qsizetype>(row) * m_nbins;
    std::memcpy(dest, line, static_cast<size_t>(m_nbins) * sizeof(uint16_t));

    m_writeIndex.store(index + 1, std::memory_order_release);
}
