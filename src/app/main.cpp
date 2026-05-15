/*! \file main.cpp
 *  \brief Точка входа приложения и регистрация типов QML.
 */
#include <QGuiApplication>
#include <QFile>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include "appstate.h"
#include "frequencygridmodel.h"
#include "frequencyviewportmodel.h"
#include "qmlsingletons.h"
#include "spectrumcontrollerstub.h"
#include "spectrumdecimator.h"
#include "waterfallcontrollerstub.h"

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

    FrequencyViewportModel viewportModel;
    FrequencyGridModel frequencyGridModel;
    SpectrumControllerStub spectrumController;
    SpectrumDecimator spectrumDecimator;
    WaterfallControllerStub waterfallController(&viewportModel);

    QObject::connect(&spectrumController,
                     &SpectrumControllerStub::bandStateChanged,
                     &waterfallController,
                     &WaterfallControllerStub::setSyntheticBand);

    siriusscope::app::AppStateQmlSingleton::instance = &AppState::instance();
    siriusscope::app::FrequencyViewportModelQmlSingleton::instance = &viewportModel;
    siriusscope::app::FrequencyGridModelQmlSingleton::instance = &frequencyGridModel;
    siriusscope::app::SpectrumControllerQmlSingleton::instance = &spectrumController;
    siriusscope::app::SpectrumDecimatorQmlSingleton::instance = &spectrumDecimator;
    siriusscope::app::WaterfallControllerQmlSingleton::instance = &waterfallController;

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
