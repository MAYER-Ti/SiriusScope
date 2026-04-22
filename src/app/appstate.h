/*! \file appstate.h
 *  \brief Хранение режима приложения и доступ к нему из QML.
 */
#ifndef APPSTATE_H
#define APPSTATE_H

#include <QObject>

/*! \class AppState
 *  \brief Хранит текущий режим приложения и экспортирует его в QML.
 *
 *  Экземпляр является синглтоном и доступен через AppState::instance().
 */
class AppState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged FINAL)

public:
    //! \brief Режимы работы приложения.
    enum class Mode : int {
        Test = 0,
        Combat = 1,
        Control = 2
    };
    Q_ENUM(Mode)

    //! \brief Возвращает текущий режим.
    //! \return Текущий режим приложения.
    inline Mode mode() const noexcept {
        return m_mode;
    };

    //! \brief Возвращает экземпляр синглтона.
    //! \return Ссылка на единственный AppState.
    static AppState& instance();

public slots:
    /*! \brief Устанавливает новый режим и уведомляет подписчиков.
     *  \param[in] newMode Новый режим приложения.
     */
    void setMode(AppState::Mode newMode);

signals:
    //! \brief Сигнал об изменении режима.
    void modeChanged(AppState::Mode mode);

private:
    //! \brief Конструирует синглтон и загружает сохраненное состояние.
    explicit AppState(QObject *parent = nullptr);
    //! \brief Загружает режим из настроек.
    void load();
    //! \brief Сохраняет режим в настройки.
    void save();

private:
    //! \brief Текущий режим.
    Mode m_mode = Mode::Test;

};

#endif // APPSTATE_H
