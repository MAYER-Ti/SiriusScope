#pragma once

#include "infrastructure/interfaces/diagnostics_sink.h"

#include <QDateTime>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <deque>

namespace siriusscope::infrastructure {
class DiagnosticLogWriter;
}

namespace siriusscope::app {

class DiagnosticsService final
    : public QObject
    , public infrastructure::IDiagnosticsSink
{
    Q_OBJECT
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastEventChanged)
    Q_PROPERTY(QString lastSubsystem READ lastSubsystem NOTIFY lastEventChanged)
    Q_PROPERTY(int lastSeverity READ lastSeverity NOTIFY lastEventChanged)
    Q_PROPERTY(qint64 lastUtcMs READ lastUtcMs NOTIFY lastEventChanged)
    Q_PROPERTY(QString lastErrorMessage READ lastErrorMessage NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastErrorSubsystem READ lastErrorSubsystem NOTIFY lastErrorChanged)
    Q_PROPERTY(qint64 lastErrorUtcMs READ lastErrorUtcMs NOTIFY lastErrorChanged)
    Q_PROPERTY(QVariantList recentEvents READ recentEvents NOTIFY recentEventsChanged)

public:
    enum Severity {
        Info = 0,
        Warning = 1,
        Error = 2,
    };
    Q_ENUM(Severity)

    explicit DiagnosticsService(infrastructure::DiagnosticLogWriter* logWriter = nullptr,
                                QObject* parent = nullptr);

    void publish(const infrastructure::DiagnosticEvent& event) override;

    QString lastMessage() const;
    QString lastSubsystem() const;
    int lastSeverity() const;
    qint64 lastUtcMs() const;

    QString lastErrorMessage() const;
    QString lastErrorSubsystem() const;
    qint64 lastErrorUtcMs() const;

    QVariantList recentEvents() const;

signals:
    void diagnosticEventPublished(QString subsystem,
                                  int severity,
                                  QString message,
                                  qint64 utcMs);
    void lastEventChanged();
    void lastErrorChanged();
    void recentEventsChanged();

private:
    struct UiDiagnosticEvent
    {
        int severity = Info;
        QString subsystem;
        QString message;
        qint64 utcMs = 0;
    };

    void acceptEvent(UiDiagnosticEvent event);
    UiDiagnosticEvent normalize(const infrastructure::DiagnosticEvent& event) const;
    QVariantMap toVariantMap(const UiDiagnosticEvent& event) const;

    infrastructure::DiagnosticLogWriter* m_logWriter = nullptr;
    UiDiagnosticEvent m_lastEvent;
    UiDiagnosticEvent m_lastError;
    std::deque<UiDiagnosticEvent> m_recentEvents;
    int m_maxRecentEvents = 100;
};

} // namespace siriusscope::app
