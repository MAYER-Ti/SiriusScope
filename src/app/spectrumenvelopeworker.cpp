#include "spectrumenvelopeworker.h"

#include <QTimerEvent>

#include <algorithm>
#include <chrono>
#include <utility>

namespace siriusscope::app {
namespace {

SpectrumEnvelopeWorkerConfig normalizeConfig(SpectrumEnvelopeWorkerConfig config)
{
    config.decayIntervalMs = std::max(1, config.decayIntervalMs);
    config.publishIntervalMs = std::max(1, config.publishIntervalMs);
    config.ingestWarningMs = std::max(1, config.ingestWarningMs);
    return config;
}

QVector<float> toQVector(const std::vector<float>& samples)
{
    QVector<float> result;
    result.reserve(static_cast<qsizetype>(samples.size()));
    for (const auto sample : samples) {
        result.push_back(sample);
    }
    return result;
}

} // namespace

SpectrumEnvelopeWorker::SpectrumEnvelopeWorker(SpectrumEnvelopeWorkerConfig config,
                                               infrastructure::IDiagnosticsSink* diagnosticsSink,
                                               QObject* parent)
    : QObject(parent)
    , m_config(normalizeConfig(std::move(config)))
    , m_diagnosticsSink(diagnosticsSink)
    , m_processor(m_config.processor)
{
    m_monotonicClock.start();
}

void SpectrumEnvelopeWorker::setViewport(double minHz, double maxHz)
{
    m_processor.setViewport(minHz, maxHz);
    m_decayTimer.stop();
    m_decayClock.invalidate();
    publishSnapshot();
}

void SpectrumEnvelopeWorker::ingestBatch(hardware::BcoSampleBatch batch)
{
    QElapsedTimer timer;
    timer.start();

    const bool changed = m_processor.ingestSamples(batch.samples, m_monotonicClock.elapsed());
    const qint64 elapsedMs = timer.elapsed();
    if (elapsedMs > m_config.ingestWarningMs) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "SpectrumEnvelopeWorker: ingestBatch took "
                    + std::to_string(elapsedMs) + " ms");
    }

    if (!m_processor.hasActiveSignal()) {
        return;
    }

    ensureDecayTimerRunning();
    if (changed) {
        scheduleSnapshot();
    }
}

void SpectrumEnvelopeWorker::reset()
{
    m_decayTimer.stop();
    m_snapshotTimer.stop();
    m_decayClock.invalidate();
    m_snapshotClock.invalidate();
    m_processor.reset();
    publishSnapshot();
}

void SpectrumEnvelopeWorker::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_decayTimer.timerId()) {
        if (!m_decayClock.isValid()) {
            m_decayClock.start();
            return;
        }

        const qint64 elapsedMs = m_decayClock.restart();
        const bool changed = m_processor.applyDecay(elapsedMs, m_monotonicClock.elapsed());
        if (changed) {
            scheduleSnapshot();
        }
        if (!m_processor.hasActiveSignal()) {
            m_decayTimer.stop();
            m_decayClock.invalidate();
        }
        return;
    }

    if (event->timerId() == m_snapshotTimer.timerId()) {
        m_snapshotTimer.stop();
        publishSnapshot();
        m_snapshotClock.restart();
        return;
    }

    QObject::timerEvent(event);
}

void SpectrumEnvelopeWorker::scheduleSnapshot()
{
    if (!m_snapshotClock.isValid()) {
        publishSnapshot();
        m_snapshotClock.restart();
        return;
    }

    const int elapsedMs = static_cast<int>(m_snapshotClock.elapsed());
    if (elapsedMs >= m_config.publishIntervalMs) {
        publishSnapshot();
        m_snapshotClock.restart();
        return;
    }

    if (!m_snapshotTimer.isActive()) {
        m_snapshotTimer.start(m_config.publishIntervalMs - elapsedMs, this);
    }
}

void SpectrumEnvelopeWorker::publishSnapshot()
{
    emit envelopeSnapshotReady(m_processor.viewMinHz(),
                               m_processor.viewMaxHz(),
                               toQVector(m_processor.samples()));
}

void SpectrumEnvelopeWorker::ensureDecayTimerRunning()
{
    if (m_config.processor.decayPerSecond <= 0.0 || m_decayTimer.isActive()) {
        return;
    }

    m_decayClock.restart();
    m_decayTimer.start(m_config.decayIntervalMs, this);
}

void SpectrumEnvelopeWorker::publish(infrastructure::DiagnosticSeverity severity,
                                     const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SpectrumEnvelopeWorker",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::app
