/*! \file main.cpp
 *  \brief Точка входа приложения и регистрация типов QML.
 */
#include <QGuiApplication>
#include <QCommandLineParser>
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

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"bco-udp-host", "Start in Hardware mode with external UDP generator at numeric IPv4 address", "host", "127.0.0.1"});
    parser.addOption({"bco-udp-port", "Generator UDP control/data port", "port", "46001"});
    parser.addOption({"bco-bind-port", "Local UDP receive port (0 = ephemeral)", "port", "46000"});
    parser.process(app);
    std::optional<siriusscope::hardware::UdpBcoSourceConfig> udpSource;
    if (parser.isSet("bco-udp-host") || parser.isSet("bco-udp-port") || parser.isSet("bco-bind-port")) {
        bool peerOk = false, bindOk = false;
        const auto peerPort = parser.value("bco-udp-port").toUInt(&peerOk);
        const auto bindPort = parser.value("bco-bind-port").toUInt(&bindOk);
        if (!peerOk || !bindOk || peerPort == 0 || peerPort > 65535 || bindPort > 65535) {
            qCritical() << "Invalid UDP port";
            return 2;
        }
        udpSource = siriusscope::hardware::UdpBcoSourceConfig{};
        udpSource->generatorHost = parser.value("bco-udp-host").toStdString();
        udpSource->generatorPort = static_cast<std::uint16_t>(peerPort);
        udpSource->bindPort = static_cast<std::uint16_t>(bindPort);
    }
    siriusscope::app::ApplicationBootstrap bootstrap(udpSource);
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
