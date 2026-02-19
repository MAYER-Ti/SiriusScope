/*!
 *  \file frequencyviewportmodel.h
 *  \brief Модель видимого диапазона частот для QML.
 */
#ifndef FREQUENCYVIEWPORTMODEL_H
#define FREQUENCYVIEWPORTMODEL_H

#include <QObject>
#include <QString>

/*!
 *  \class FrequencyViewportModel
 *  \brief Управляет видимым диапазоном частот и ограничивает его глобальными границами.
 */
class FrequencyViewportModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double viewMinHz READ viewMinHz WRITE setViewMinHz NOTIFY viewportChanged FINAL)
    Q_PROPERTY(double viewMaxHz READ viewMaxHz WRITE setViewMaxHz NOTIFY viewportChanged FINAL)
    Q_PROPERTY(double globalMinHz READ globalMinHz CONSTANT)
    Q_PROPERTY(double globalMaxHz READ globalMaxHz CONSTANT)

public:
    //! \brief Конструирует модель с границами по умолчанию.
    explicit FrequencyViewportModel(QObject *parent = nullptr);

    //! \brief Возвращает текущую минимальную видимую частоту в Гц.
    //! \return Минимальная частота обзора.
    double viewMinHz() const noexcept { return m_viewMinHz; }
    //! \brief Возвращает текущую максимальную видимую частоту в Гц.
    //! \return Максимальная частота обзора.
    double viewMaxHz() const noexcept { return m_viewMaxHz; }
    //! \brief Возвращает глобальную минимальную границу в Гц.
    //! \return Глобальная минимальная частота.
    double globalMinHz() const noexcept { return m_globalMinHz; }
    //! \brief Возвращает глобальную максимальную границу в Гц.
    //! \return Глобальная максимальная частота.
    double globalMaxHz() const noexcept { return m_globalMaxHz; }

    /*!
     *  \brief Устанавливает обе границы, нормализует порядок и применяет ограничения.
     *  \param[in] minHz Нижняя граница видимого диапазона, Гц.
     *  \param[in] maxHz Верхняя граница видимого диапазона, Гц.
     *  \param[in] sourceTag Источник изменения (для отладки/маркировки).
     */
    Q_INVOKABLE void setViewport(double minHz, double maxHz, const QString &sourceTag = QString());

public slots:
    /*!
     *  \brief Обновляет нижнюю границу видимого диапазона.
     *  \param[in] minHz Нижняя граница видимого диапазона, Гц.
     */
    void setViewMinHz(double minHz);
    /*!
     *  \brief Обновляет верхнюю границу видимого диапазона.
     *  \param[in] maxHz Верхняя граница видимого диапазона, Гц.
     */
    void setViewMaxHz(double maxHz);

signals:
    /*!
     *  \brief Сигнал об изменении видимого диапазона.
     *  \param[in] minHz Новая нижняя граница, Гц.
     *  \param[in] maxHz Новая верхняя граница, Гц.
     *  \param[in] sourceTag Источник изменения.
     */
    void viewportChanged(double minHz, double maxHz, const QString &sourceTag);

private:
    /*!
     *  \brief Применяет нормализацию и при необходимости отправляет сигнал.
     *  \param[in] minHz Нижняя граница.
     *  \param[in] maxHz Верхняя граница.
     *  \param[in] sourceTag Источник изменения.
     */
    void applyViewport(double minHz, double maxHz, const QString &sourceTag);

    //! \brief Текущая нижняя граница обзора.
    double m_viewMinHz = 300e6;
    //! \brief Текущая верхняя граница обзора.
    double m_viewMaxHz = 18e9;
    //! \brief Глобальная нижняя граница.
    const double m_globalMinHz = 300e6;
    //! \brief Глобальная верхняя граница.
    const double m_globalMaxHz = 18e9;
};

#endif // FREQUENCYVIEWPORTMODEL_H
