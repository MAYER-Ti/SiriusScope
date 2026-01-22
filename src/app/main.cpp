#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    //QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);

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
