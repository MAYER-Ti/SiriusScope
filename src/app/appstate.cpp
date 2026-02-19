/*! \file appstate.cpp
 *  \brief Реализация AppState.
 */
#include "appstate.h"

#include <QSettings>

//! \brief Возвращает экземпляр синглтона.
AppState &AppState::instance()
{
    static AppState inst;
    return inst;
}

/*! \brief Устанавливает новый режим и отправляет modeChanged при изменении.
 *  \param[in] newMode Новый режим приложения.
 */
void AppState::setMode(Mode newMode)
{
    if (m_mode == newMode)
        return;
    m_mode = newMode;

    emit modeChanged(m_mode);
}

//! \brief Конструирует синглтон и загружает сохраненное состояние.
AppState::AppState(QObject *parent)
    : QObject{parent}
{
    load();
}

//! \brief Загружает режим из QSettings.
void AppState::load()
{
    QSettings s; // Заглужка для интеграции с QSettings
    const int v = s.value(QStringLiteral("app/mode"), static_cast<int>(Mode::Test)).toInt();
    // Проверяем invalid/out-of-range значение, если оно вне диапазона enum Mode.
    if (v < static_cast<int>(Mode::Test) || v > static_cast<int>(Mode::Control)) {
        m_mode = Mode::Test;
    }
    else {
        m_mode = static_cast<Mode>(v);
    }
}

//! \brief Сохраняет текущий режим в QSettings.
void AppState::save()
{
    QSettings s; // Заглужка для интеграции с QSettings
    s.setValue(QStringLiteral("app/mode"), static_cast<int>(m_mode));
}
