#include "diagnosticsservice.h"

#include "infrastructure/logging/diagnostic_log_writer.h"

#include <QMetaObject>
#include <QTimeZone>

#include <chrono>
#include <utility>

namespace siriusscope::app {
namespace {

int severityToInt(infrastructure::DiagnosticSeverity severity)
{
    switch (severity) {
    case infrastructure::DiagnosticSeverity::Info:
        return DiagnosticsService::Info;
    case infrastructure::DiagnosticSeverity::Warning:
        return DiagnosticsService::Warning;
    case infrastructure::DiagnosticSeverity::Error:
        return DiagnosticsService::Error;
    }
    return DiagnosticsService::Warning;
}

qint64 utcMs(std::chrono::system_clock::time_point timestamp)
{
    const auto normalized =
        timestamp == std::chrono::system_clock::time_point{}
        ? std::chrono::system_clock::now()
        : timestamp;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        normalized.time_since_epoch());
    return static_cast<qint64>(ms.count());
}

infrastructure::DiagnosticEvent normalizedEvent(const infrastructure::DiagnosticEvent& event)
{
    infrastructure::DiagnosticEvent normalized = event;
    if (normalized.timestamp == std::chrono::system_clock::time_point{}) {
        normalized.timestamp = std::chrono::system_clock::now();
    }
    return normalized;
}

} // namespace

DiagnosticsService::DiagnosticsService(infrastructure::DiagnosticLogWriter* logWriter,
                                       QObject* parent)
    : QObject(parent)
    , m_logWriter(logWriter)
{
}

void DiagnosticsService::publish(const infrastructure::DiagnosticEvent& event)
{
    const auto normalized = normalizedEvent(event);
    if (m_logWriter) {
        m_logWriter->append(normalized);
    }

    auto uiEvent = normalize(normalized);
    QMetaObject::invokeMethod(this,
                              [this, uiEvent = std::move(uiEvent)]() mutable {
                                  acceptEvent(std::move(uiEvent));
                              },
                              Qt::QueuedConnection);
}

QString DiagnosticsService::lastMessage() const
{
    return m_lastEvent.message;
}

QString DiagnosticsService::lastSubsystem() const
{
    return m_lastEvent.subsystem;
}

int DiagnosticsService::lastSeverity() const
{
    return m_lastEvent.severity;
}

qint64 DiagnosticsService::lastUtcMs() const
{
    return m_lastEvent.utcMs;
}

QString DiagnosticsService::lastErrorMessage() const
{
    return m_lastError.message;
}

QString DiagnosticsService::lastErrorSubsystem() const
{
    return m_lastError.subsystem;
}

qint64 DiagnosticsService::lastErrorUtcMs() const
{
    return m_lastError.utcMs;
}

QVariantList DiagnosticsService::recentEvents() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(m_recentEvents.size()));
    for (const auto& event : m_recentEvents) {
        result.push_back(toVariantMap(event));
    }
    return result;
}

void DiagnosticsService::acceptEvent(UiDiagnosticEvent event)
{
    m_lastEvent = event;
    m_recentEvents.push_back(event);
    while (m_recentEvents.size() > static_cast<std::size_t>(m_maxRecentEvents)) {
        m_recentEvents.pop_front();
    }

    emit diagnosticEventPublished(event.subsystem, event.severity, event.message, event.utcMs);
    emit lastEventChanged();
    emit recentEventsChanged();

    if (event.severity == Error) {
        m_lastError = event;
        emit lastErrorChanged();
    }
}

DiagnosticsService::UiDiagnosticEvent DiagnosticsService::normalize(
    const infrastructure::DiagnosticEvent& event) const
{
    return UiDiagnosticEvent{
        severityToInt(event.severity),
        QString::fromStdString(event.subsystem),
        QString::fromStdString(event.message),
        utcMs(event.timestamp),
    };
}

QVariantMap DiagnosticsService::toVariantMap(const UiDiagnosticEvent& event) const
{
    QVariantMap result;
    result.insert(QStringLiteral("severity"), event.severity);
    result.insert(QStringLiteral("subsystem"), event.subsystem);
    result.insert(QStringLiteral("message"), event.message);
    result.insert(QStringLiteral("utcMs"), event.utcMs);
    return result;
}

} // namespace siriusscope::app
