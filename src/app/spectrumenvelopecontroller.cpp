#include "spectrumenvelopecontroller.h"

#include <algorithm>
#include <chrono>

namespace siriusscope::app {
namespace {

SpectrumEnvelopeControllerConfig normalizeConfig(SpectrumEnvelopeControllerConfig config)
{
    config.binCount = std::max(1, config.binCount);
    config.publishIntervalMs = std::max(1, config.publishIntervalMs);
    return config;
}

} // namespace

SpectrumEnvelopeController::SpectrumEnvelopeController(QObject* parent)
    : SpectrumEnvelopeController(SpectrumEnvelopeControllerConfig{}, parent)
{
}

SpectrumEnvelopeController::SpectrumEnvelopeController(SpectrumEnvelopeControllerConfig config,
                                                       QObject* parent)
    : QObject(parent)
    , m_config(normalizeConfig(config))
    , m_samples(m_config.binCount, 0.0F)
    , m_pendingSamples(m_config.binCount, 0.0F)
{
    m_publishTimer.setInterval(m_config.publishIntervalMs);
    m_publishTimer.setSingleShot(true);
    connect(&m_publishTimer,
            &QTimer::timeout,
            this,
            &SpectrumEnvelopeController::publishPendingEnvelope);
    m_publishRateClock.start();
}

void SpectrumEnvelopeController::setDiagnosticsSink(
    infrastructure::IDiagnosticsSink* diagnosticsSink) noexcept
{
    m_diagnosticsSink = diagnosticsSink;
}

void SpectrumEnvelopeController::acceptSnapshot(double minHz,
                                                double maxHz,
                                                const QVector<float>& samples)
{
    m_pendingMinHz = minHz;
    m_pendingMaxHz = maxHz;
    m_pendingSamples = samples;
    m_publishPending = true;
    schedulePublish();
}

QVariantList SpectrumEnvelopeController::envelopeSamples() const
{
    return buildSamples();
}

void SpectrumEnvelopeController::clear()
{
    m_publishTimer.stop();
    m_samples.fill(0.0F);
    m_pendingSamples.fill(0.0F);
    m_pendingMinHz = m_viewMinHz;
    m_pendingMaxHz = m_viewMaxHz;
    m_publishPending = false;
    publishEnvelope();
}

void SpectrumEnvelopeController::publishPendingEnvelope()
{
    if (!m_publishPending) {
        return;
    }

    m_publishPending = false;
    m_viewMinHz = m_pendingMinHz;
    m_viewMaxHz = m_pendingMaxHz;
    m_samples = m_pendingSamples;
    publishEnvelope();
    m_publishClock.restart();
}

QVariantList SpectrumEnvelopeController::buildSamples() const
{
    QVariantList samples;
    samples.reserve(m_samples.size());
    for (const auto value : m_samples) {
        samples.append(value);
    }
    return samples;
}

void SpectrumEnvelopeController::schedulePublish()
{
    if (!m_publishClock.isValid()) {
        publishPendingEnvelope();
        m_publishClock.restart();
        return;
    }

    const int elapsedMs = static_cast<int>(m_publishClock.elapsed());
    if (elapsedMs >= m_config.publishIntervalMs) {
        publishPendingEnvelope();
        m_publishClock.restart();
        return;
    }

    if (!m_publishTimer.isActive()) {
        m_publishTimer.start(m_config.publishIntervalMs - elapsedMs);
    }
}

void SpectrumEnvelopeController::publishEnvelope()
{
    emit envelopeChanged(m_viewMinHz, m_viewMaxHz, buildSamples());

    ++m_publishCount;
    if (m_publishRateClock.elapsed() < 1000) {
        return;
    }

    publish(infrastructure::DiagnosticSeverity::Info,
            "SpectrumEnvelopeController: publish rate = "
                + std::to_string(m_publishCount) + " fps");
    m_publishCount = 0;
    m_publishRateClock.restart();
}

void SpectrumEnvelopeController::publish(infrastructure::DiagnosticSeverity severity,
                                         const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SpectrumEnvelopeController",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::app
