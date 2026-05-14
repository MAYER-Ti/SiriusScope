/*!
 *  \file waterfallcontrollerstub.h
 *  \brief Контроллер-заглушка, генерирующий демонстрационные строки Waterfall.
 */
#ifndef WATERFALLCONTROLLERSTUB_H
#define WATERFALLCONTROLLERSTUB_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <cstdint>
#include <memory>

#include "waterfallhistorymodel.h"
#include "waterfallstorage.h"

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
    Q_PROPERTY(bool liveMode READ liveMode NOTIFY liveModeChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyLoadingChanged)
    Q_PROPERTY(QString currentUtcText READ currentUtcText NOTIFY currentUtcTextChanged)
    Q_PROPERTY(qulonglong timeTicksVersion READ timeTicksVersion NOTIFY timeTicksChanged)

public:
    enum class WaterfallViewMode
    {
        Live,
        History
    };

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
    bool liveMode() const noexcept;
    bool historyLoading() const noexcept { return m_historyLoading; }
    QString currentUtcText() const;
    qulonglong timeTicksVersion() const noexcept { return m_timeTicksVersion; }

    Q_INVOKABLE QVariantList visibleTimeTicks(int pixelHeight) const;
    Q_INVOKABLE void scrollHistory(int wheelSteps);
    Q_INVOKABLE void jumpToLive();

signals:
    void liveModeChanged();
    void historyLoadingChanged();
    void currentUtcTextChanged();
    void timeTicksChanged();

private slots:
    void onViewportChanged(double minHz, double maxHz, const QString &sourceTag);
    void commitViewport();
    void pushSyntheticLine();

private:
    void scheduleRetune(double minHz, double maxHz);
    WaterfallRow buildLine(double minHz, double maxHz, qint64 utcMs);
    void reloadHistoryFromStorage();
    void setHistoryLoading(bool loading);
    void updateRenderBuffer();
    void notifyPresentationChanged(bool previousLiveMode, const QString& previousUtcText);
    void seedSyntheticHistory();

    FrequencyViewportModel *m_viewportModel = nullptr;
    WaterfallRingBuffer *m_ringBuffer = nullptr;
    std::unique_ptr<InMemoryWaterfallStorage> m_storage;
    WaterfallHistoryModel m_historyModel;
    QTimer m_retuneTimer;
    QTimer m_lineTimer;
    QVector<uint16_t> m_lineBuffer;
    uint64_t m_generationId = 0;
    quint64 m_nextSampleIndex = 0;
    qulonglong m_timeTicksVersion = 0;
    double m_viewMinHz = 0.0;
    double m_viewMaxHz = 0.0;
    double m_phase = 0.0;
    bool m_retuning = false;
    bool m_historyLoading = false;
};

#endif // WATERFALLCONTROLLERSTUB_H
