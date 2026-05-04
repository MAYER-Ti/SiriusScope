/*!
 *  \file waterfallcontrollerstub.h
 *  \brief Stub controller generating waterfall rows for demo.
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
 *  \brief Generates synthetic waterfall rows and handles viewport retune.
 */
class WaterfallControllerStub : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *ringBuffer READ ringBuffer CONSTANT)

public:
    explicit WaterfallControllerStub(FrequencyViewportModel *viewportModel,
                                     QObject *parent = nullptr);

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
