/*!
 *  \file spectrumcontrollerstub.h
 *  \brief Заглушка контроллера спектра и обработки диапазонов.
 */
#ifndef SPECTRUMCONTROLLERSTUB_H
#define SPECTRUMCONTROLLERSTUB_H

#include <QObject>
#include <QVariantList>

/*!
 *  \class SpectrumControllerStub
 *  \brief Генерирует синтетический спектр и отражает изменения диапазонов.
 */
class SpectrumControllerStub : public QObject
{
    Q_OBJECT

public:
    //! \brief Конструирует заглушку контроллера.
    explicit SpectrumControllerStub(QObject *parent = nullptr);

public slots:
    /*!
     *  \brief Генерирует синтетический спектр для заданного диапазона.
     *  \param[in] viewMinHz Нижняя граница обзора, Гц.
     *  \param[in] viewMaxHz Верхняя граница обзора, Гц.
     */
    void requestSpectrum(double viewMinHz, double viewMaxHz);
    /*!
     *  \brief Принимает обновление параметров полосы от UI.
     *  \param[in] bandId Идентификатор полосы.
     *  \param[in] centerHz Центральная частота, Гц.
     *  \param[in] widthHz Ширина полосы, Гц.
     *  \param[in] isFinal Признак финального подтверждения изменения.
     */
    void setBand(int bandId, double centerHz, double widthHz, bool isFinal);
    /*!
     *  \brief Принимает обновление порога для полосы.
     *  \param[in] bandId Идентификатор полосы.
     *  \param[in] thresholdDb Порог, дБ.
     *  \param[in] isFinal Признак финального подтверждения изменения.
     */
    void setBandThreshold(int bandId, double thresholdDb, bool isFinal);
    /*!
     *  \brief Включает или выключает полосу.
     *  \param[in] bandId Идентификатор полосы.
     *  \param[in] enabled Включена ли полоса.
     */
    void setBandEnabled(int bandId, bool enabled);

signals:
    /*!
     *  \brief Сигнал о готовом спектре.
     *  \param[in] viewMinHz Нижняя граница обзора, Гц.
     *  \param[in] viewMaxHz Верхняя граница обзора, Гц.
     *  \param[in] samples Список значений спектра.
     *  \param[in] minDb Минимальное значение, дБ.
     *  \param[in] maxDb Максимальное значение, дБ.
     */
    void spectrumReady(double viewMinHz, double viewMaxHz, const QVariantList &samples, float minDb, float maxDb);
    /*!
     *  \brief Сигнал об изменении параметров полосы.
     *  \param[in] bandId Идентификатор полосы.
     *  \param[in] centerHz Центральная частота, Гц.
     *  \param[in] widthHz Ширина полосы, Гц.
     *  \param[in] thresholdDb Порог, дБ.
     *  \param[in] enabled Признак включения.
     */
    void bandStateChanged(int bandId, double centerHz, double widthHz, double thresholdDb, bool enabled);

private:
    /*!
     *  \brief Формирует синтетические значения спектра и определяет min/max.
     *  \param[in] viewMinHz Нижняя граница обзора, Гц.
     *  \param[in] viewMaxHz Верхняя граница обзора, Гц.
     *  \param[out] outMinDb Минимальное значение, дБ.
     *  \param[out] outMaxDb Максимальное значение, дБ.
     *  \return Список значений спектра.
     */
    QVariantList generateSpectrum(double viewMinHz, double viewMaxHz, float &outMinDb, float &outMaxDb) const;
};

#endif // SPECTRUMCONTROLLERSTUB_H
