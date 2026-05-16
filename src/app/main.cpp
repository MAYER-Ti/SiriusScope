/*! \file main.cpp
 *  \brief Точка входа приложения и регистрация типов QML.
 */
#include <QGuiApplication>
#include <QFile>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include "applicationbootstrap.h"

/*! \brief Инициализирует Qt/QML и запускает цикл обработки событий.
 *  \param[in] argc Количество аргументов командной строки.
 *  \param[in] argv Массив аргументов командной строки.
 *  \return Код завершения приложения.
 */
int main(int argc, char *argv[])
{
    qputenv("QT_QUICK_CONTROLS_STYLE", QByteArrayLiteral("Basic"));

    QGuiApplication app(argc, argv);

    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    siriusscope::app::ApplicationBootstrap bootstrap;
    bootstrap.registerQmlSingletons();

#ifdef QT_DEBUG
    qDebug() << "waterfall.vert.qsb exists"
             << QFile(QStringLiteral(":/SiriusScope/shaders/waterfall.vert.qsb")).exists();
    qDebug() << "waterfall.frag.qsb exists"
             << QFile(QStringLiteral(":/SiriusScope/shaders/waterfall.frag.qsb")).exists();
#endif

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("SiriusScope", "Main");

    return app.exec();
}
