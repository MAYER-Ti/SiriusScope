/*!
 *  \file waterfallitem.h
 *  \brief QQuickItem для отрисовки текстуры Waterfall через граф сцены.
 */
#ifndef WATERFALLITEM_H
#define WATERFALLITEM_H

#include <QMetaObject>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

#include "waterfallcolormapper.h"

class WaterfallRingBuffer;

/*!
 *  \class WaterfallItem
 *  \brief GPU-рендерер Waterfall, использующий общий кольцевой буфер.
 */
class WaterfallItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *ringBuffer READ ringBuffer WRITE setRingBuffer NOTIFY ringBufferChanged)
    Q_PROPERTY(bool freshData READ freshData NOTIFY freshDataChanged)
    Q_PROPERTY(qulonglong activeGenerationId READ activeGenerationId NOTIFY activeGenerationIdChanged)
    Q_PROPERTY(bool directionalEnabled READ directionalEnabled WRITE setDirectionalEnabled NOTIFY colorParamsChanged)
    Q_PROPERTY(double colorGamma READ colorGamma WRITE setColorGamma NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionDeadZone READ directionDeadZone WRITE setDirectionDeadZone NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionalAlpha READ directionalAlpha WRITE setDirectionalAlpha NOTIFY colorParamsChanged)

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
    bool directionalEnabled() const noexcept { return m_colorParams.directionalEnabled; }
    void setDirectionalEnabled(bool enabled);
    double colorGamma() const noexcept { return m_colorParams.gamma; }
    void setColorGamma(double gamma);
    double directionDeadZone() const noexcept { return m_colorParams.directionDeadZone; }
    void setDirectionDeadZone(double deadZone);
    double directionalAlpha() const noexcept { return m_colorParams.directionalAlpha; }
    void setDirectionalAlpha(double alpha);

signals:
    //! \brief Испускается при изменении подключенного кольцевого буфера.
    void ringBufferChanged();
    //! \brief Испускается при изменении freshData.
    void freshDataChanged();
    //! \brief Испускается при изменении activeGenerationId.
    void activeGenerationIdChanged();
    void colorParamsChanged();

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
    QMetaObject::Connection m_ringBufferConnection;
    WaterfallColorParams m_colorParams;
    uint64_t m_colorRevision = 0;
    bool m_freshData = false;
    qulonglong m_activeGenerationId = 0;
};

#endif // WATERFALLITEM_H
