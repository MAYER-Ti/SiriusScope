/*!
 *  \file waterfallitem.h
 *  \brief QQuickItem для отрисовки текстуры Waterfall через граф сцены.
 */
#ifndef WATERFALLITEM_H
#define WATERFALLITEM_H

#include <QQuickItem>
#include <QTimer>

class WaterfallRingBuffer;

/*!
 *  \class WaterfallItem
 *  \brief GPU-рендерер Waterfall, использующий общий кольцевой буфер.
 */
class WaterfallItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *ringBuffer READ ringBuffer WRITE setRingBuffer NOTIFY ringBufferChanged)
    Q_PROPERTY(bool freshData READ freshData NOTIFY freshDataChanged)
    Q_PROPERTY(qulonglong activeGenerationId READ activeGenerationId NOTIFY activeGenerationIdChanged)

public:
    /*!
     *  \brief Создает пустой элемент Waterfall с отрисовкой через граф сцены.
     *  \param[in] parent Необязательный родительский элемент.
     */
    explicit WaterfallItem(QQuickItem *parent = nullptr);

    /*!
     *  \brief Возвращает текущий подключенный кольцевой буфер Waterfall как QObject.
     *  \return Объект буфера или nullptr, если буфер не подключен.
     */
    QObject *ringBuffer() const;
    /*!
     *  \brief Подключает экземпляр WaterfallRingBuffer, переданный из QML.
     *  \param[in] buffer QObject, который должен быть WaterfallRingBuffer.
     */
    void setRingBuffer(QObject *buffer);

    /*!
     *  \brief Показывает, получил ли элемент данные для активного поколения.
     *  \return true, если свежие данные относятся к activeGenerationId().
     */
    bool freshData() const noexcept { return m_freshData; }
    /*!
     *  \brief Возвращает идентификатор поколения, которое сейчас отрисовывается.
     *  \return Идентификатор активного поколения Waterfall.
     */
    qulonglong activeGenerationId() const noexcept { return m_activeGenerationId; }

signals:
    //! \brief Испускается при изменении подключенного кольцевого буфера.
    void ringBufferChanged();
    //! \brief Испускается при изменении freshData.
    void freshDataChanged();
    //! \brief Испускается при изменении activeGenerationId.
    void activeGenerationIdChanged();

private slots:
    void notifyFreshData(qulonglong generationId);
    void notifyStale(qulonglong generationId);

protected:
    /*!
     *  \brief Обновляет узел графа сцены Qt Quick для текущего состояния буфера.
     *  \param[in] oldNode Существующий узел графа сцены, если он есть.
     *  \param[in] data Данные обновления узла отрисовки Qt Quick.
     *  \return Узел графа сцены для отрисовки.
     */
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    WaterfallRingBuffer *m_ringBuffer = nullptr;
    bool m_freshData = false;
    qulonglong m_activeGenerationId = 0;
    QTimer m_updateTimer;
};

#endif // WATERFALLITEM_H
