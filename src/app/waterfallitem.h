/*!
 *  \file waterfallitem.h
 *  \brief QQuickItem rendering waterfall texture via scene graph.
 */
#ifndef WATERFALLITEM_H
#define WATERFALLITEM_H

#include <QQuickItem>
#include <QTimer>

class WaterfallRingBuffer;

/*!
 *  \class WaterfallItem
 *  \brief GPU-backed waterfall renderer using a shared ring buffer.
 */
class WaterfallItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *ringBuffer READ ringBuffer WRITE setRingBuffer NOTIFY ringBufferChanged)
    Q_PROPERTY(bool freshData READ freshData NOTIFY freshDataChanged)
    Q_PROPERTY(qulonglong activeGenerationId READ activeGenerationId NOTIFY activeGenerationIdChanged)

public:
    /*!
     *  \brief Creates an empty scene-graph-backed waterfall item.
     *  \param[in] parent Optional parent item.
     */
    explicit WaterfallItem(QQuickItem *parent = nullptr);

    /*!
     *  \brief Returns the currently attached waterfall ring buffer as QObject.
     *  \return Ring buffer object, or nullptr when no buffer is attached.
     */
    QObject *ringBuffer() const;
    /*!
     *  \brief Attaches a WaterfallRingBuffer instance supplied from QML.
     *  \param[in] buffer QObject expected to be a WaterfallRingBuffer.
     */
    void setRingBuffer(QObject *buffer);

    /*!
     *  \brief Indicates whether the item has received data for the active generation.
     *  \return true when recent data belongs to activeGenerationId().
     */
    bool freshData() const noexcept { return m_freshData; }
    /*!
     *  \brief Returns the generation identifier currently rendered by the item.
     *  \return Active waterfall generation id.
     */
    qulonglong activeGenerationId() const noexcept { return m_activeGenerationId; }

signals:
    //! \brief Emitted when the attached ring buffer changes.
    void ringBufferChanged();
    //! \brief Emitted when freshData changes.
    void freshDataChanged();
    //! \brief Emitted when activeGenerationId changes.
    void activeGenerationIdChanged();

private slots:
    void notifyFreshData(qulonglong generationId);
    void notifyStale(qulonglong generationId);

protected:
    /*!
     *  \brief Updates the Qt Quick scene graph node for the current buffer state.
     *  \param[in] oldNode Existing scene graph node, if any.
     *  \param[in] data Qt Quick paint-node update data.
     *  \return Scene graph node to use for rendering.
     */
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    WaterfallRingBuffer *m_ringBuffer = nullptr;
    bool m_freshData = false;
    qulonglong m_activeGenerationId = 0;
    QTimer m_updateTimer;
};

#endif // WATERFALLITEM_H
