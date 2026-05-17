#include "infrastructure/logging/diagnostic_log_writer.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <chrono>

namespace siriusscope::infrastructure {
namespace {

QString severityName(DiagnosticSeverity severity)
{
    switch (severity) {
    case DiagnosticSeverity::Info:
        return QStringLiteral("Info");
    case DiagnosticSeverity::Warning:
        return QStringLiteral("Warning");
    case DiagnosticSeverity::Error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

std::chrono::system_clock::time_point normalizedTimestamp(
    std::chrono::system_clock::time_point timestamp)
{
    if (timestamp != std::chrono::system_clock::time_point{}) {
        return timestamp;
    }
    return std::chrono::system_clock::now();
}

qint64 utcMs(std::chrono::system_clock::time_point timestamp)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        normalizedTimestamp(timestamp).time_since_epoch());
    return static_cast<qint64>(ms.count());
}

QString resolvedDataRootPath(const QString& dataRootPath)
{
    if (!dataRootPath.trimmed().isEmpty()) {
        return dataRootPath;
    }
    return QDir(QDir::currentPath()).filePath(QStringLiteral("SiriusScopeData"));
}

QString logLine(const DiagnosticEvent& event)
{
    const qint64 timestampMs = utcMs(event.timestamp);
    const QDateTime timestamp =
        QDateTime::fromMSecsSinceEpoch(timestampMs, QTimeZone::UTC);
    return QStringLiteral("%1 [%2] %3: %4\n")
        .arg(timestamp.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'")),
             severityName(event.severity),
             QString::fromStdString(event.subsystem),
             QString::fromStdString(event.message));
}

QString logPathForEvent(const QString& logsRootPath, const DiagnosticEvent& event)
{
    const qint64 timestampMs = utcMs(event.timestamp);
    const QDate date = QDateTime::fromMSecsSinceEpoch(timestampMs, QTimeZone::UTC).date();
    const QString fileName = QStringLiteral("app_%1.log")
                                 .arg(date.toString(QStringLiteral("yyyy-MM-dd")));
    return QDir(logsRootPath).filePath(fileName);
}

} // namespace

DiagnosticLogWriter::DiagnosticLogWriter(Config config)
    : m_config(std::move(config))
    , m_logsRootPath(QDir(resolvedDataRootPath(m_config.dataRootPath))
                         .filePath(QStringLiteral("logs")))
{
    m_config.maxQueuedEvents = std::max(1, m_config.maxQueuedEvents);
    m_worker = std::thread(&DiagnosticLogWriter::writerLoop, this);
}

DiagnosticLogWriter::~DiagnosticLogWriter()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopRequested = true;
    }
    m_condition.notify_all();

    if (m_worker.joinable() && m_worker.get_id() != std::this_thread::get_id()) {
        m_worker.join();
    }
}

void DiagnosticLogWriter::append(const DiagnosticEvent& event)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_queue.size() >= static_cast<std::size_t>(m_config.maxQueuedEvents)) {
            m_queue.pop_front();
            ++m_droppedEvents;
        }
        m_queue.push_back(event);
    }
    m_condition.notify_one();
}

void DiagnosticLogWriter::writerLoop()
{
    QDir().mkpath(m_logsRootPath);

    for (;;) {
        DiagnosticEvent event;
        bool hasEvent = false;
        std::size_t droppedEvents = 0;

        {
            std::unique_lock lock(m_mutex);
            m_condition.wait(lock, [this] {
                return m_stopRequested || !m_queue.empty() || m_droppedEvents > 0;
            });

            if (m_droppedEvents > 0) {
                droppedEvents = m_droppedEvents;
                m_droppedEvents = 0;
            } else if (!m_queue.empty()) {
                event = std::move(m_queue.front());
                m_queue.pop_front();
                hasEvent = true;
            } else if (m_stopRequested) {
                break;
            }
        }

        if (droppedEvents > 0) {
            writeEvent(DiagnosticEvent{
                DiagnosticSeverity::Warning,
                "DiagnosticLogWriter",
                "diagnostic log queue overflow: dropped "
                    + std::to_string(droppedEvents) + " oldest event(s)",
                std::chrono::system_clock::now(),
            });
            continue;
        }

        if (hasEvent) {
            writeEvent(event);
        }
    }
}

void DiagnosticLogWriter::writeEvent(const DiagnosticEvent& event)
{
    QDir().mkpath(m_logsRootPath);

    QFile file(logPathForEvent(m_logsRootPath, event));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << logLine(event);
    stream.flush();
}

} // namespace siriusscope::infrastructure
