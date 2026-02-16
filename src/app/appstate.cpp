#include "appstate.h"

#include <QSettings>

AppState &AppState::instance()
{
    static AppState inst;
    return inst;
}

void AppState::setMode(Mode newMode)
{
    if (m_mode == newMode)
        return;
    m_mode = newMode;

    emit modeChanged(m_mode);
}

AppState::AppState(QObject *parent)
    : QObject{parent}
{
    load();
}

void AppState::load()
{
    QSettings s; // Заглужка для интеграции с QSettings
    const int v = s.value(QStringLiteral("app/mode"), static_cast<int>(Mode::Test)).toInt();
    if ((static_cast<int>(Mode::Test) < v) || ( v < static_cast<int>(Mode::Control))) {
        m_mode = Mode::Test;
    }
    else {
        m_mode = static_cast<Mode>(v);
    }
}

void AppState::save()
{
    QSettings s; // Заглужка для интеграции с QSettings
    s.setValue(QStringLiteral("app/mode"), static_cast<int>(m_mode));
}
