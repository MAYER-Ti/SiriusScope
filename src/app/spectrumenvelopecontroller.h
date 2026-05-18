#pragma once

#include "infrastructure/interfaces/diagnostics_sink.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QVariantList>

#include <string>

namespace siriusscope::app {

struct SpectrumEnvelopeControllerConfig
{
    int binCount = 1024;
    int publishIntervalMs = 66;
};

class SpectrumEnvelopeController final : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumEnvelopeController(QObject* parent = nullptr);
    explicit SpectrumEnvelopeController(SpectrumEnvelopeControllerConfig config,
                                        QObject* parent = nullptr);

    void setDiagnosticsSink(infrastructure::IDiagnosticsSink* diagnosticsSink) noexcept;
    void acceptSnapshot(double minHz, double maxHz, const QVector<float>& samples);

    Q_INVOKABLE QVariantList envelopeSamples() const;

signals:
    void envelopeChanged(double minHz, double maxHz, const QVariantList& samples);

private slots:
    void publishPendingEnvelope();

private:
    QVariantList buildSamples() const;
    void schedulePublish();
    void publishEnvelope();
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    SpectrumEnvelopeControllerConfig m_config;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    QTimer m_publishTimer;
    QElapsedTimer m_publishClock;
    QElapsedTimer m_publishRateClock;
    QVector<float> m_samples;
    QVector<float> m_pendingSamples;
    double m_viewMinHz = 0.0;
    double m_viewMaxHz = 0.0;
    double m_pendingMinHz = 0.0;
    double m_pendingMaxHz = 0.0;
    int m_publishCount = 0;
    bool m_publishPending = false;
};

} // namespace siriusscope::app
