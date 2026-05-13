/*!
 *  \file waterfallitem.cpp
 *  \brief Реализация рендерера данных Waterfall через граф сцены.
 */
#include "waterfallitem.h"

#include "waterfallringbuffer.h"

#include <QImage>
#include <QMatrix4x4>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>

#include <cstring>

namespace {
constexpr int kUniformDataSize = 80;
}

class WaterfallMaterialShader;

class WaterfallMaterial final : public QSGMaterial
{
public:
    WaterfallMaterial() = default;
    ~WaterfallMaterial() override = default;

    QSGMaterialType *type() const override
    {
        static QSGMaterialType type;
        return &type;
    }

    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    int compare(const QSGMaterial *other) const override
    {
        auto *o = static_cast<const WaterfallMaterial *>(other);
        if (m_texture != o->m_texture) {
            return m_texture < o->m_texture ? -1 : 1;
        }
        if (m_rowOffset == o->m_rowOffset) {
            return 0;
        }
        return m_rowOffset < o->m_rowOffset ? -1 : 1;
    }

    void setTexture(QSGTexture *texture) { m_texture = texture; }
    QSGTexture *texture() const { return m_texture; }

    void setRowOffset(float offset) { m_rowOffset = offset; }
    float rowOffset() const { return m_rowOffset; }

private:
    QSGTexture *m_texture = nullptr;
    float m_rowOffset = 0.0f;
};

class WaterfallMaterialShader final : public QSGMaterialShader
{
public:
    WaterfallMaterialShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/SiriusScope/shaders/waterfall.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/SiriusScope/shaders/waterfall.frag.qsb"));
    }

    bool updateUniformData(RenderState &state,
                           QSGMaterial *newMaterial,
                           QSGMaterial *oldMaterial) override
    {
        Q_UNUSED(oldMaterial)
        auto *material = static_cast<WaterfallMaterial *>(newMaterial);
        QByteArray *buf = state.uniformData();
        if (buf->size() != kUniformDataSize) {
            buf->resize(kUniformDataSize);
        }

        if (state.isMatrixDirty()) {
            const QMatrix4x4 m = state.combinedMatrix();
            std::memcpy(buf->data(), m.constData(), 64);
        }

        const float opacity = state.opacity();
        const float params[4] = { opacity, material->rowOffset(), 0.0f, 0.0f };
        std::memcpy(buf->data() + 64, params, sizeof(params));
        return true;
    }

    void updateSampledImage(RenderState &,
                            int binding,
                            QSGTexture **texture,
                            QSGMaterial *newMaterial,
                            QSGMaterial *) override
    {
        if (binding != 1) {
            return;
        }
        auto *material = static_cast<WaterfallMaterial *>(newMaterial);
        *texture = material->texture();
    }

    int uniformDataSize() const
    {
        return kUniformDataSize;
    }
};

QSGMaterialShader *WaterfallMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new WaterfallMaterialShader();
}

struct WaterfallNode : public QSGGeometryNode
{
    WaterfallNode()
        : geometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4)
    {
        setGeometry(&geometry);
        setFlag(QSGNode::OwnsGeometry, true);
        geometry.setDrawingMode(QSGGeometry::DrawTriangleStrip);
    }

    ~WaterfallNode() override
    {
        delete texture;
    }

    QSGGeometry geometry;
    WaterfallMaterial *material = nullptr;
    QSGTexture *texture = nullptr;
    QImage image;
    uint64_t lastWriteIndex = 0;
    uint64_t lastGenerationId = 0;
    bool awaitingFirstLine = true;
    int nbins = 0;
    int height = 0;
};

WaterfallItem::WaterfallItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    m_updateTimer.setInterval(16);
    m_updateTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_updateTimer, &QTimer::timeout, this, [this]() { update(); });
    m_updateTimer.start();
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
    m_ringBuffer = qobject_cast<WaterfallRingBuffer *>(buffer);
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

        node->image = QImage(nbins, rows, QImage::Format_Grayscale16);
        node->image.fill(0);

        node->material = new WaterfallMaterial();
        node->setMaterial(node->material);
        node->setFlag(QSGNode::OwnsMaterial, true);
    }

    QSGGeometry::updateTexturedRectGeometry(&node->geometry,
                                            QRectF(0.0, 0.0, width(), height()),
                                            QRectF(0.0, 0.0, 1.0, 1.0));
    node->markDirty(QSGNode::DirtyGeometry);

    const uint64_t writeIndex = m_ringBuffer->writeIndex();
    const uint64_t generationId = m_ringBuffer->generationId();

    bool textureDirty = false;
    if (generationId != node->lastGenerationId) {
        node->lastGenerationId = generationId;
        node->lastWriteIndex = writeIndex;
        node->awaitingFirstLine = true;
        node->image.fill(0);
        textureDirty = true;
        QMetaObject::invokeMethod(this,
                                  "notifyStale",
                                  Qt::QueuedConnection,
                                  Q_ARG(qulonglong, generationId));
    }

    bool updated = false;
    if (writeIndex != node->lastWriteIndex) {
        uint64_t start = node->lastWriteIndex;
        uint64_t end = writeIndex;
        if (end - start > static_cast<uint64_t>(rows)) {
            start = end - static_cast<uint64_t>(rows);
        }
        for (uint64_t i = start; i < end; ++i) {
            const int row = static_cast<int>(i % static_cast<uint64_t>(rows));
            const uint16_t *src = m_ringBuffer->linePtr(row);
            if (!src) {
                continue;
            }
            uint16_t *dst = reinterpret_cast<uint16_t *>(node->image.scanLine(row));
            std::memcpy(dst, src, static_cast<size_t>(nbins) * sizeof(uint16_t));
        }
        node->lastWriteIndex = end;
        updated = true;
        textureDirty = true;

        if (node->awaitingFirstLine && end > start) {
            node->awaitingFirstLine = false;
            QMetaObject::invokeMethod(this,
                                      "notifyFreshData",
                                      Qt::QueuedConnection,
                                      Q_ARG(qulonglong, generationId));
        }
    }

    float rowOffset = 0.0f;
    if (rows > 0 && writeIndex > 0) {
        const uint64_t latestRow =
            (writeIndex + static_cast<uint64_t>(rows) - 1) % static_cast<uint64_t>(rows);
        rowOffset = static_cast<float>(latestRow) / static_cast<float>(rows);
    }
    node->material->setRowOffset(rowOffset);
    node->markDirty(QSGNode::DirtyMaterial);

    if (textureDirty) {
        if (node->texture) {
            delete node->texture;
            node->texture = nullptr;
        }
    }

    if (!node->texture) {
        QQuickWindow *quickWindow = window();
        if (quickWindow) {
            node->texture = quickWindow->createTextureFromImage(node->image);
            if (node->texture) {
                node->texture->setFiltering(QSGTexture::Linear);
                node->texture->setHorizontalWrapMode(QSGTexture::ClampToEdge);
                node->texture->setVerticalWrapMode(QSGTexture::ClampToEdge);
            }
        }
        if (node->material) {
            node->material->setTexture(node->texture);
        }
    }

    return node;
}
