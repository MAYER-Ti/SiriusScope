/*! \file main.cpp
 *  \brief Точка входа приложения и регистрация типов QML.
 */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include "appstate.h"
#include "frequencyviewportmodel.h"
#include "spectrumcontrollerstub.h"
#include "spectrumdecimator.h"

/*! \brief Инициализирует Qt/QML и запускает цикл обработки событий.
 *  \param[in] argc Количество аргументов командной строки.
 *  \param[in] argv Массив аргументов командной строки.
 *  \return Код завершения приложения.
 */
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

    qmlRegisterSingletonInstance(
        "SiriusScope",
        1, 0,
        "AppState",
        &AppState::instance()
        );

    FrequencyViewportModel viewportModel;
    SpectrumControllerStub spectrumController;
    SpectrumDecimator spectrumDecimator;

    qmlRegisterSingletonInstance(
        "SiriusScope",
        1, 0,
        "FrequencyViewportModel",
        &viewportModel
        );

    qmlRegisterSingletonInstance(
        "SiriusScope",
        1, 0,
        "SpectrumController",
        &spectrumController
        );

    qmlRegisterSingletonInstance(
        "SiriusScope",
        1, 0,
        "SpectrumDecimator",
        &spectrumDecimator
        );

    engine.loadFromModule("SiriusScope", "Main");

    return app.exec();
}
