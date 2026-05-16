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

#include "syntheticwaterfalldatasource.h"
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
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY recordingStateChanged)
    Q_PROPERTY(QString recordingStatusText READ recordingStatusText NOTIFY recordingStateChanged)
    Q_PROPERTY(QString recordingLabel READ recordingLabel NOTIFY viewportChanged)
    Q_PROPERTY(QString viewportModeText READ viewportModeText NOTIFY viewportChanged)
    Q_PROPERTY(qint64 viewportTopUtcMs READ viewportTopUtcMs NOTIFY viewportChanged)
    Q_PROPERTY(qint64 viewportBottomUtcMs READ viewportBottomUtcMs NOTIFY viewportChanged)
    Q_PROPERTY(int visibleRowCount READ visibleRowCount CONSTANT)
    Q_PROPERTY(qulonglong timeTicksVersion READ timeTicksVersion NOTIFY timeTicksChanged)
    Q_PROPERTY(bool directionalEnabled READ directionalEnabled NOTIFY colorParamsChanged)
    Q_PROPERTY(double colorGamma READ colorGamma NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionDeadZone READ directionDeadZone NOTIFY colorParamsChanged)
    Q_PROPERTY(double directionalAlpha READ directionalAlpha NOTIFY colorParamsChanged)

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
    bool sessionActive() const noexcept { return m_sessionActive; }
    QString recordingStatusText() const;
    QString recordingLabel() const;
    QString viewportModeText() const;
    qint64 viewportTopUtcMs() const;
    qint64 viewportBottomUtcMs() const;
    int visibleRowCount() const noexcept { return m_historyModel.visibleRowCount(); }
    qulonglong timeTicksVersion() const noexcept { return m_timeTicksVersion; }
    bool directionalEnabled() const noexcept { return true; }
    double colorGamma() const noexcept { return 0.7; }
    double directionDeadZone() const noexcept { return 0.10; }
    double directionalAlpha() const noexcept { return 0.35; }

    Q_INVOKABLE QVariantList visibleTimeTicks(int pixelHeight) const;
    Q_INVOKABLE void scrollHistory(int wheelSteps);
    Q_INVOKABLE void jumpToLive();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

public slots:
    void setSyntheticBand(int bandId, double centerHz, double widthHz, double thresholdAmplitude, bool enabled);

signals:
    void liveModeChanged();
    void historyLoadingChanged();
    void currentUtcTextChanged();
    void recordingStateChanged();
    void viewportChanged();
    void timeTicksChanged();
    void colorParamsChanged();

private slots:
    void onViewportChanged(double minHz, double maxHz, const QString &sourceTag);
    void commitViewport();
    void pushSyntheticLine();

private:
    void scheduleRetune(double minHz, double maxHz);
    WaterfallRow buildLine(double minHz, double maxHz, qint64 utcMs);
    void reloadHistoryFromStorage();
    QVector<SyntheticBandRange> currentBands() const;
    void setHistoryLoading(bool loading);
    void updateRenderBuffer();
    void notifyPresentationChanged(bool previousLiveMode, const QString& previousUtcText);
    void seedSyntheticHistory();

    FrequencyViewportModel *m_viewportModel = nullptr;
    WaterfallRingBuffer *m_ringBuffer = nullptr;
    std::unique_ptr<InMemoryWaterfallStorage> m_storage;
    SyntheticWaterfallDataSource m_syntheticSource;
    QVector<SyntheticBandRange> m_syntheticBands;
    WaterfallHistoryModel m_historyModel;
    QTimer m_retuneTimer;
    QTimer m_lineTimer;
    uint64_t m_generationId = 0;
    quint64 m_nextSampleIndex = 0;
    qulonglong m_timeTicksVersion = 0;
    double m_viewMinHz = 0.0;
    double m_viewMaxHz = 0.0;
    double m_sourceMinHz = 300e6;
    double m_sourceMaxHz = 18e9;
    bool m_retuning = false;
    bool m_historyLoading = false;
    bool m_sessionActive = false;
};

#endif // WATERFALLCONTROLLERSTUB_H
