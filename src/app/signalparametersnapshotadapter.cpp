#include "signalparametersnapshotadapter.h"

#include "app/scancontroller.h"
#include "pipeline/data_ingest_pipeline.h"

#include <algorithm>
#include <chrono>

namespace siriusscope::app {
namespace {

SignalParameterSnapshotAdapterConfig normalizeConfig(
    SignalParameterSnapshotAdapterConfig config)
{
    config.pollIntervalMs = std::max(1, config.pollIntervalMs);
    return config;
}

} // namespace

SignalParameterSnapshotAdapter::SignalParameterSnapshotAdapter(
    ScanController* scanController,
    pipeline::DataIngestPipeline* dataIngestPipeline,
    infrastructure::IDiagnosticsSink* diagnosticsSink,
    SignalParameterSnapshotAdapterConfig config,
    QObject* parent)
    : QObject(parent)
    , m_scanController(scanController)
    , m_dataIngestPipeline(dataIngestPipeline)
    , m_diagnosticsSink(diagnosticsSink)
    , m_config(normalizeConfig(config))
{
    m_pollTimer.setInterval(m_config.pollIntervalMs);
    connect(&m_pollTimer,
            &QTimer::timeout,
            this,
            &SignalParameterSnapshotAdapter::pollLatestSnapshot);
}

void SignalParameterSnapshotAdapter::start()
{
    if (!m_scanController || !m_dataIngestPipeline) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "SignalParameterSnapshotAdapter start skipped: dependencies are not configured");
        return;
    }
    if (!m_pollTimer.isActive()) {
        m_pollTimer.start();
    }
}

void SignalParameterSnapshotAdapter::stop()
{
    m_pollTimer.stop();
}

bool SignalParameterSnapshotAdapter::running() const noexcept
{
    return m_pollTimer.isActive();
}

void SignalParameterSnapshotAdapter::pollLatestSnapshot()
{
    if (!m_scanController || !m_dataIngestPipeline || !m_scanController->scanActive()) {
        return;
    }

    const auto snapshot = m_dataIngestPipeline->latestSignalParameterSnapshot();
    if (!snapshot || snapshot->sequenceId == m_lastSnapshotSequenceId) {
        return;
    }

    m_lastSnapshotSequenceId = snapshot->sequenceId;
    m_scanController->acceptSignalParameterSnapshot(snapshot);
}

void SignalParameterSnapshotAdapter::publish(infrastructure::DiagnosticSeverity severity,
                                             const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SignalParameterSnapshotAdapter",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::app
