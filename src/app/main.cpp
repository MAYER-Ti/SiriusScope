#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include "appstate.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // Регистрируем singleton в QML
    qmlRegisterSingletonInstance(
        "SiriusScope",  // URI
        1, 0,           // версия
        "AppState",     // имя в QML
        &AppState::instance()
        );

    engine.loadFromModule("SiriusScope", "Main");

    return app.exec();
}
