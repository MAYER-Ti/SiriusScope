/*!
 *  \file waterfallcontrollerstub.h
 *  \brief Контроллер-заглушка, генерирующий демонстрационные строки Waterfall.
 */
#ifndef WATERFALLCONTROLLERSTUB_H
#define WATERFALLCONTROLLERSTUB_H

#include <QObject>
#include <QTimer>
#include <QVector>

#include <cstdint>

class FrequencyViewportModel;
class WaterfallRingBuffer;

/*!
 *  \class WaterfallControllerStub
 *  \brief Генерирует синтетические строки Waterfall и обрабатывает перестройку обзора.
 */
class WaterfallControllerStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *ringBuffer READ ringBuffer CONSTANT)

public:
    /*!
     *  \brief Создает демонстрационный контроллер, связанный с моделью обзора частот.
     *  \param[in] viewportModel Источник изменений видимого частотного диапазона.
     *  \param[in] parent Необязательный родительский объект Qt.
     */
    explicit WaterfallControllerStub(FrequencyViewportModel *viewportModel,
                                     QObject *parent = nullptr);

    /*!
     *  \brief Возвращает QObject-обертку общего кольцевого буфера Waterfall.
     *  \return Кольцевой буфер, экспортируемый в QML и WaterfallItem.
     */
    QObject *ringBuffer() const;

private slots:
    void onViewportChanged(double minHz, double maxHz, const QString &sourceTag);
    void commitViewport();
    void pushSyntheticLine();

private:
    void scheduleRetune(double minHz, double maxHz);
    void buildLine(double minHz, double maxHz);

    FrequencyViewportModel *m_viewportModel = nullptr;
    WaterfallRingBuffer *m_ringBuffer = nullptr;
    QTimer m_retuneTimer;
    QTimer m_lineTimer;
    QVector<uint16_t> m_lineBuffer;
    uint64_t m_generationId = 0;
    double m_viewMinHz = 0.0;
    double m_viewMaxHz = 0.0;
    double m_phase = 0.0;
    bool m_retuning = false;
};

#endif // WATERFALLCONTROLLERSTUB_H
