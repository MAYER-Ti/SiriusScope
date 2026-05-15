/*!
 *  \file waterfallitem.cpp
 *  \brief Реализация рендерера данных Waterfall через граф сцены.
 */
#include "waterfallitem.h"

#include "waterfallringbuffer.h"
#include "waterfallstorage.h"

#include <QDebug>
#include <QImage>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QVector>

#include <algorithm>

struct WaterfallNode : public QSGSimpleTextureNode
{
    QImage image;
    QVector<WaterfallBeamBin> scratchLine;
    uint64_t lastWriteIndex = 0;
    uint64_t lastGenerationId = 0;
    uint64_t lastColorRevision = 0;
    bool awaitingFirstLine = true;
    int nbins = 0;
    int height = 0;
    bool loggedFirstTexture = false;
};

WaterfallItem::WaterfallItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QObject *WaterfallItem::ringBuffer() const
{
    return m_ringBuffer;
}

void WaterfallItem::setRingBuffer(QObject *buffer)
{
    if (m_ringBuffer == buffer) {
        return;
    }
    if (m_ringBufferConnection) {
        QObject::disconnect(m_ringBufferConnection);
        m_ringBufferConnection = {};
    }

    m_ringBuffer = qobject_cast<WaterfallRingBuffer *>(buffer);
    if (m_ringBuffer) {
        m_ringBufferConnection = connect(m_ringBuffer,
                                         &WaterfallRingBuffer::contentsChanged,
                                         this,
                                         [this]() { update(); },
                                         Qt::QueuedConnection);
    }

    m_freshData = false;
    m_activeGenerationId = 0;
    emit ringBufferChanged();
    emit freshDataChanged();
    emit activeGenerationIdChanged();
    update();
}

void WaterfallItem::setDirectionalEnabled(bool enabled)
{
    if (m_colorParams.directionalEnabled == enabled) {
        return;
    }
    m_colorParams.directionalEnabled = enabled;
    ++m_colorRevision;
    emit colorParamsChanged();
    update();
}

void WaterfallItem::setColorGamma(double gamma)
{
    if (qFuzzyCompare(m_colorParams.gamma, gamma)) {
        return;
    }
    m_colorParams.gamma = gamma;
    ++m_colorRevision;
    emit colorParamsChanged();
    update();
}

void WaterfallItem::setDirectionDeadZone(double deadZone)
{
    if (qFuzzyCompare(m_colorParams.directionDeadZone, deadZone)) {
        return;
    }
    m_colorParams.directionDeadZone = deadZone;
    ++m_colorRevision;
    emit colorParamsChanged();
    update();
}

void WaterfallItem::setDirectionalAlpha(double alpha)
{
    if (qFuzzyCompare(m_colorParams.directionalAlpha, alpha)) {
        return;
    }
    m_colorParams.directionalAlpha = alpha;
    ++m_colorRevision;
    emit colorParamsChanged();
    update();
}

void WaterfallItem::notifyFreshData(qulonglong generationId)
{
    if (m_activeGenerationId != generationId) {
        m_activeGenerationId = generationId;
        emit activeGenerationIdChanged();
    }
    if (!m_freshData) {
        m_freshData = true;
        emit freshDataChanged();
    }
}

void WaterfallItem::notifyStale(qulonglong generationId)
{
    if (m_activeGenerationId != generationId) {
        m_activeGenerationId = generationId;
        emit activeGenerationIdChanged();
    }
    if (m_freshData) {
        m_freshData = false;
        emit freshDataChanged();
    }
}

QSGNode *WaterfallItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<WaterfallNode *>(oldNode);
    if (!m_ringBuffer || width() <= 0.0 || height() <= 0.0) {
        delete node;
        return nullptr;
    }

    const int nbins = m_ringBuffer->nbins();
    const int rows = m_ringBuffer->height();
    if (nbins <= 0 || rows <= 0) {
        delete node;
        return nullptr;
    }

    if (!node || node->nbins != nbins || node->height != rows) {
        delete node;
        node = new WaterfallNode();
        node->nbins = nbins;
        node->height = rows;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        node->setTextureCoordinatesTransform(QSGSimpleTextureNode::NoTransform);

        node->image = QImage(nbins, rows, QImage::Format_RGBA8888);
        node->image.fill(0);

        node->scratchLine.resize(nbins);
    }

    node->setRect(0.0, 0.0, width(), height());
    node->setSourceRect(0.0, 0.0,
                        static_cast<qreal>(nbins),
                        static_cast<qreal>(rows));

    const uint64_t writeIndex = m_ringBuffer->writeIndex();
    const uint64_t generationId = m_ringBuffer->generationId();

    bool textureDirty = false;
    uint16_t maxValue = 0;
    if (generationId != node->lastGenerationId
        || writeIndex != node->lastWriteIndex
        || m_colorRevision != node->lastColorRevision) {
        node->lastGenerationId = generationId;
        node->lastWriteIndex = writeIndex;
        node->lastColorRevision = m_colorRevision;
        node->image.fill(0);

        for (int row = 0; row < rows; ++row) {
            node->scratchLine.fill(WaterfallBeamBin{});
            m_ringBuffer->copyLine(row, node->scratchLine.data(), nbins);

            uchar *dst = node->image.scanLine(row);
            for (int bin = 0; bin < nbins; ++bin) {
                const WaterfallBeamBin sample = node->scratchLine.at(bin);
                maxValue = std::max(maxValue, std::max(sample.left, sample.right));
                const Rgba8 color = WaterfallColorMapper::map(sample, m_colorParams);
                const int offset = bin * 4;
                dst[offset] = color.r;
                dst[offset + 1] = color.g;
                dst[offset + 2] = color.b;
                dst[offset + 3] = color.a;
            }
        }

        textureDirty = true;

        if (m_ringBuffer->populatedRows() > 0) {
            node->awaitingFirstLine = false;
            QMetaObject::invokeMethod(this,
                                      "notifyFreshData",
                                      Qt::QueuedConnection,
                                      Q_ARG(qulonglong, generationId));
        } else {
            node->awaitingFirstLine = true;
            QMetaObject::invokeMethod(this,
                                      "notifyStale",
                                      Qt::QueuedConnection,
                                      Q_ARG(qulonglong, generationId));
        }
    }

    if (textureDirty) {
        QSGTexture *newTexture = nullptr;
        QQuickWindow *quickWindow = window();
        if (quickWindow) {
            newTexture = quickWindow->createTextureFromImage(node->image);
            if (newTexture) {
                newTexture->setFiltering(QSGTexture::Linear);
                newTexture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
                newTexture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            }
        }
        node->setTexture(newTexture);

#ifdef QT_DEBUG
        if (!node->loggedFirstTexture) {
            qDebug() << "WaterfallItem texture"
                     << "rows" << rows
                     << "bins" << nbins
                     << "populatedRows" << m_ringBuffer->populatedRows()
                     << "maxValue" << maxValue;
            node->loggedFirstTexture = true;
        }
        if (!newTexture) {
            qWarning() << "WaterfallItem failed to create scene graph texture";
        }
        if (m_ringBuffer->populatedRows() > 0 && maxValue == 0) {
            qWarning() << "WaterfallItem render buffer contains only zero samples";
        }
#endif
    }

    return node;
}
