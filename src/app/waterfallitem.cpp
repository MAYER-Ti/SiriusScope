/*!
 *  \file waterfallitem.cpp
 *  \brief Реализация рендерера данных Waterfall через граф сцены.
 */
#include "waterfallitem.h"

#include "waterfallringbuffer.h"

#include <QDebug>
#include <QImage>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QVector>

#include <algorithm>

namespace {

struct ColorStop
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
};

uchar channelToByte(double value)
{
    return static_cast<uchar>(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5);
}

ColorStop mixColor(ColorStop a, ColorStop b, double factor)
{
    factor = std::clamp(factor, 0.0, 1.0);
    return ColorStop{
        a.r + (b.r - a.r) * factor,
        a.g + (b.g - a.g) * factor,
        a.b + (b.b - a.b) * factor
    };
}

ColorStop waterfallColor(uint16_t sample)
{
    const double t = std::clamp(static_cast<double>(sample) / 65535.0, 0.0, 1.0);
    constexpr ColorStop c0{0.02, 0.03, 0.08};
    constexpr ColorStop c1{0.00, 0.22, 0.60};
    constexpr ColorStop c2{0.00, 0.75, 0.80};
    constexpr ColorStop c3{0.95, 0.85, 0.30};
    constexpr ColorStop c4{1.00, 1.00, 1.00};

    if (t < 0.25) {
        return mixColor(c0, c1, t / 0.25);
    }
    if (t < 0.55) {
        return mixColor(c1, c2, (t - 0.25) / 0.30);
    }
    if (t < 0.85) {
        return mixColor(c2, c3, (t - 0.55) / 0.30);
    }
    return mixColor(c3, c4, (t - 0.85) / 0.15);
}

} // namespace

struct WaterfallNode : public QSGSimpleTextureNode
{
    QImage image;
    QVector<uint16_t> scratchLine;
    uint64_t lastWriteIndex = 0;
    uint64_t lastGenerationId = 0;
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
    if (generationId != node->lastGenerationId || writeIndex != node->lastWriteIndex) {
        node->lastGenerationId = generationId;
        node->lastWriteIndex = writeIndex;
        node->image.fill(0);

        for (int row = 0; row < rows; ++row) {
            node->scratchLine.fill(0);
            m_ringBuffer->copyLine(row, node->scratchLine.data(), nbins);

            uchar *dst = node->image.scanLine(row);
            for (int bin = 0; bin < nbins; ++bin) {
                const uint16_t sample = node->scratchLine.at(bin);
                maxValue = std::max(maxValue, sample);
                const ColorStop color = waterfallColor(sample);
                const int offset = bin * 4;
                dst[offset] = channelToByte(color.r);
                dst[offset + 1] = channelToByte(color.g);
                dst[offset + 2] = channelToByte(color.b);
                dst[offset + 3] = 255;
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
