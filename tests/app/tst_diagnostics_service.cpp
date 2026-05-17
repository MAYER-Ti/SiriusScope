#include "app/diagnosticsservice.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace {

using namespace siriusscope;

class TestRunner
{
public:
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            ++m_failed;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 1500)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

void testPublishUpdatesLastEvent(TestRunner& test)
{
    app::DiagnosticsService service;
    int publishedCount = 0;
    QObject::connect(&service,
                     &app::DiagnosticsService::diagnosticEventPublished,
                     [&publishedCount](const QString&, int, const QString&, qint64) {
                         ++publishedCount;
                     });

    service.publish(infrastructure::DiagnosticEvent{
        infrastructure::DiagnosticSeverity::Info,
        "Application",
        "service started",
        std::chrono::system_clock::now(),
    });

    const bool delivered = waitUntil([&service] {
        return service.lastMessage() == QStringLiteral("service started");
    });

    test.require(delivered, "publish updates last message");
    test.require(service.lastSubsystem() == QStringLiteral("Application"),
                 "publish updates last subsystem");
    test.require(service.lastSeverity() == app::DiagnosticsService::Info,
                 "publish updates last severity");
    test.require(publishedCount == 1, "publish emits diagnosticEventPublished");
}

void testWarningAndErrorState(TestRunner& test)
{
    app::DiagnosticsService service;

    service.publish(infrastructure::DiagnosticEvent{
        infrastructure::DiagnosticSeverity::Warning,
        "WaterfallProcessing",
        "dropped batch",
        std::chrono::system_clock::now(),
    });
    service.publish(infrastructure::DiagnosticEvent{
        infrastructure::DiagnosticSeverity::Error,
        "WaterfallStorage",
        "Cannot write waterfall.bin",
        std::chrono::system_clock::now(),
    });

    const bool delivered = waitUntil([&service] {
        return service.lastErrorMessage() == QStringLiteral("Cannot write waterfall.bin");
    });

    test.require(delivered, "error updates last error message");
    test.require(service.lastErrorSubsystem() == QStringLiteral("WaterfallStorage"),
                 "error updates last error subsystem");
    test.require(service.recentEvents().size() == 2,
                 "warning and error are stored in recent events");
}

void testMissingTimestampIsNormalized(TestRunner& test)
{
    app::DiagnosticsService service;
    service.publish(infrastructure::DiagnosticEvent{
        infrastructure::DiagnosticSeverity::Info,
        "Test",
        "timestamp normalized",
        {},
    });

    const bool delivered = waitUntil([&service] {
        return service.lastUtcMs() > 0;
    });

    test.require(delivered, "empty timestamp is replaced with current UTC milliseconds");
}

void testRecentEventsAreBounded(TestRunner& test)
{
    app::DiagnosticsService service;
    for (int i = 0; i < 105; ++i) {
        service.publish(infrastructure::DiagnosticEvent{
            infrastructure::DiagnosticSeverity::Info,
            "Test",
            "event " + std::to_string(i),
            std::chrono::system_clock::now(),
        });
    }

    const bool delivered = waitUntil([&service] {
        return service.recentEvents().size() == 100
            && service.lastMessage() == QStringLiteral("event 104");
    });

    test.require(delivered, "recent events are capped at 100 items");
}

void testPublishFromWorkerThread(TestRunner& test)
{
    app::DiagnosticsService service;
    std::thread worker([&service] {
        service.publish(infrastructure::DiagnosticEvent{
            infrastructure::DiagnosticSeverity::Warning,
            "Worker",
            "thread event",
            std::chrono::system_clock::now(),
        });
    });
    worker.join();

    const bool delivered = waitUntil([&service] {
        return service.lastMessage() == QStringLiteral("thread event");
    });

    test.require(delivered, "worker-thread publish is delivered through event loop");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testPublishUpdatesLastEvent(test);
    testWarningAndErrorState(test);
    testMissingTimestampIsNormalized(test);
    testRecentEventsAreBounded(test);
    testPublishFromWorkerThread(test);

    return test.result();
}
