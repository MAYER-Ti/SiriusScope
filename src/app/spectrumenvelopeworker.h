#pragma once

#include "hardware/interfaces/bco_sample_source.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "processing/spectrum_envelope_processor.h"

#include <QBasicTimer>
#include <QElapsedTimer>
#include <QObject>
#include <QVector>

#include <string>

namespace siriusscope::app {

struct SpectrumEnvelopeWorkerConfig
{
    processing::SpectrumEnvelopeProcessorConfig processor;
    int decayIntervalMs = 33;
    int publishIntervalMs = 66;
    int ingestWarningMs = 10;
};

class SpectrumEnvelopeWorker final : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumEnvelopeWorker(
        SpectrumEnvelopeWorkerConfig config = {},
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr,
        QObject* parent = nullptr);

public slots:
    void setViewport(double minHz, double maxHz);
    void ingestBatch(hardware::BcoSampleBatch batch);

signals:
    void envelopeSnapshotReady(double minHz, double maxHz, QVector<float> samples);

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    void scheduleSnapshot();
    void publishSnapshot();
    void ensureDecayTimerRunning();
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    SpectrumEnvelopeWorkerConfig m_config;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    processing::SpectrumEnvelopeProcessor m_processor;
    QBasicTimer m_decayTimer;
    QBasicTimer m_snapshotTimer;
    QElapsedTimer m_monotonicClock;
    QElapsedTimer m_decayClock;
    QElapsedTimer m_snapshotClock;
};

} // namespace siriusscope::app
