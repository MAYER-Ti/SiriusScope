/*!
 *  \file waterfallringbuffer.h
 *  \brief Кольцевой буфер строк Waterfall, безопасный для чтения из пользовательского интерфейса.
 */
#ifndef WATERFALLRINGBUFFER_H
#define WATERFALLRINGBUFFER_H

#include <QObject>
#include <QMutex>
#include <QVector>

#include <atomic>
#include <cstdint>

#include "waterfallstorage.h"

/*!
 *  \class WaterfallRingBuffer
 *  \brief Хранит строки Waterfall в кольцевом буфере с атомарными индексами.
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
     *  \brief Создает кольцевой буфер фиксированного размера для строк Waterfall.
     *  \param[in] nbins Количество частотных бинов в строке.
     *  \param[in] height Количество строк, сохраняемых в памяти.
     *  \param[in] globalMinHz Нижняя глобальная граница частоты, Гц.
     *  \param[in] globalMaxHz Верхняя глобальная граница частоты, Гц.
     *  \param[in] parent Необязательный родительский объект Qt.
     */
    explicit WaterfallRingBuffer(int nbins,
                                 int height,
                                 double globalMinHz = 0.0,
                                 double globalMaxHz = 0.0,
                                 QObject *parent = nullptr);

    /*!
     *  \brief Добавляет одну строку и продвигает атомарный индекс записи.
     *  \param[in] line Указатель на nbins значений амплитуды/цвета.
     *  \param[in] nbins Количество значений в line.
     *  \param[in] generationId Идентификатор поколения обзора/данных для строки.
     */
    void pushLine(const WaterfallBeamBin *line, int nbins, uint64_t generationId);

    void replaceRows(const QVector<WaterfallRow>& rows, uint64_t generationId);
    void replaceSlots(const QVector<WaterfallRowSlot>& rowSlots, uint64_t generationId);

    /*!
     *  \brief Возвращает следующую позицию записи как монотонно растущий индекс.
     *  \return Атомарный индекс записи.
     */
    uint64_t writeIndex() const noexcept
    {
        return m_writeIndex.load(std::memory_order_acquire);
    }
    /*!
     *  \brief Возвращает последний идентификатор поколения обзора/данных.
     *  \return Атомарный идентификатор поколения.
     */
    uint64_t generationId() const noexcept
    {
        return m_generationId.load(std::memory_order_acquire);
    }
    /*!
     *  \brief Возвращает количество частотных бинов в строке.
     *  \return Ширина строки в бинах.
     */
    int nbins() const noexcept { return m_nbins; }
    /*!
     *  \brief Возвращает количество сохраняемых строк.
     *  \return Высота кольцевого буфера в строках.
     */
    int height() const noexcept { return m_height; }
    int populatedRows() const noexcept
    {
        return m_populatedRows.load(std::memory_order_acquire);
    }
    /*!
     *  \brief Возвращает нижнюю глобальную границу частоты.
     *  \return Частота, Гц.
     */
    double globalMinHz() const noexcept { return m_globalMinHz; }
    /*!
     *  \brief Возвращает верхнюю глобальную границу частоты.
     *  \return Частота, Гц.
     */
    double globalMaxHz() const noexcept { return m_globalMaxHz; }

    /*!
     *  \brief Возвращает указатель на сохраненную строку по физическому индексу.
     *  \param[in] row Физический индекс строки внутри [0, height()).
     *  \return Указатель на первое значение строки или nullptr для неверной строки.
     */
    const WaterfallBeamBin *linePtr(int row) const noexcept;
    bool copyLine(int row, WaterfallBeamBin *destination, int nbins) const;

signals:
    void contentsChanged();

private:
    QVector<WaterfallBeamBin> m_data;
    std::atomic<uint64_t> m_writeIndex{0};
    std::atomic<uint64_t> m_generationId{0};
    std::atomic<int> m_populatedRows{0};
    mutable QMutex m_mutex;
    int m_nbins = 0;
    int m_height = 0;
    double m_globalMinHz = 0.0;
    double m_globalMaxHz = 0.0;
};

#endif // WATERFALLRINGBUFFER_H
