#include "infrastructure/logging/diagnostic_log_writer.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimeZone>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

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

std::chrono::system_clock::time_point fixedTimestamp()
{
    return std::chrono::system_clock::time_point{
        std::chrono::milliseconds{1'704'067'200'123LL},
    };
}

QString expectedLogPath(const QString& dataRootPath)
{
    return QDir(QDir(dataRootPath).filePath(QStringLiteral("logs")))
        .filePath(QStringLiteral("app_2024-01-01.log"));
}

QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    return stream.readAll();
}

void testWritesDiagnosticLine(TestRunner& test)
{
    QTemporaryDir tempDir;
    test.require(tempDir.isValid(), "temporary log directory is valid");

    const QString logPath = expectedLogPath(tempDir.path());
    {
        infrastructure::DiagnosticLogWriter writer({
            tempDir.path(),
            8,
        });
        writer.append(infrastructure::DiagnosticEvent{
            infrastructure::DiagnosticSeverity::Warning,
            "WaterfallStorage",
            "Cannot open waterfall.idx",
            fixedTimestamp(),
        });
    }

    const QString text = readTextFile(logPath);
    test.require(QDir(tempDir.path()).exists(QStringLiteral("logs")),
                 "writer creates logs directory");
    test.require(QFile::exists(logPath), "writer creates dated app log file");
    test.require(text.contains(QStringLiteral("[Warning] WaterfallStorage: Cannot open")),
                 "writer stores severity subsystem and message");
    test.require(text.contains(QStringLiteral("2024-01-01T00:00:00.123Z")),
                 "writer stores UTC timestamp with milliseconds");
}

void testDestructorFlushesQueue(TestRunner& test)
{
    QTemporaryDir tempDir;
    test.require(tempDir.isValid(), "temporary flush directory is valid");

    const QString logPath = expectedLogPath(tempDir.path());
    {
        infrastructure::DiagnosticLogWriter writer({
            tempDir.path(),
            32,
        });
        writer.append(infrastructure::DiagnosticEvent{
            infrastructure::DiagnosticSeverity::Info,
            "Application",
            "queued event",
            fixedTimestamp(),
        });
    }

    const QString text = readTextFile(logPath);
    test.require(text.contains(QStringLiteral("queued event")),
                 "writer destructor drains queued events");
}

void testEmptyDataRootDoesNotCrash(TestRunner& test)
{
    {
        infrastructure::DiagnosticLogWriter writer({
            QString(),
            4,
        });
        writer.append(infrastructure::DiagnosticEvent{
            infrastructure::DiagnosticSeverity::Info,
            "Test",
            "empty root",
            fixedTimestamp(),
        });
    }

    test.require(true, "empty data root is accepted");
}

} // namespace

int main()
{
    TestRunner test;

    testWritesDiagnosticLine(test);
    testDestructorFlushesQueue(test);
    testEmptyDataRootDoesNotCrash(test);

    return test.result();
}
