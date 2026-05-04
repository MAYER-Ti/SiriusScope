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
    explicit WaterfallItem(QQuickItem *parent = nullptr);

    QObject *ringBuffer() const;
    void setRingBuffer(QObject *buffer);

    bool freshData() const noexcept { return m_freshData; }
    qulonglong activeGenerationId() const noexcept { return m_activeGenerationId; }

signals:
    void ringBufferChanged();
    void freshDataChanged();
    void activeGenerationIdChanged();

private slots:
    void notifyFreshData(qulonglong generationId);
    void notifyStale(qulonglong generationId);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;

private:
    WaterfallRingBuffer *m_ringBuffer = nullptr;
    bool m_freshData = false;
    qulonglong m_activeGenerationId = 0;
    QTimer m_updateTimer;
};

#endif // WATERFALLITEM_H
