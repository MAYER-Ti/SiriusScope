#include "bearingsnapshotadapter.h"

#include "app/scancontroller.h"
#include "pipeline/data_ingest_pipeline.h"

#include <algorithm>
#include <chrono>

namespace siriusscope::app {
namespace {

BearingSnapshotAdapterConfig normalizeConfig(BearingSnapshotAdapterConfig config)
{
    config.pollIntervalMs = std::max(1, config.pollIntervalMs);
    return config;
}

} // namespace

BearingSnapshotAdapter::BearingSnapshotAdapter(
    ScanController* scanController,
    pipeline::DataIngestPipeline* dataIngestPipeline,
    infrastructure::IDiagnosticsSink* diagnosticsSink,
    BearingSnapshotAdapterConfig config,
    QObject* parent)
    : QObject(parent)
    , m_scanController(scanController)
    , m_dataIngestPipeline(dataIngestPipeline)
    , m_diagnosticsSink(diagnosticsSink)
    , m_config(normalizeConfig(config))
{
    m_pollTimer.setInterval(m_config.pollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &BearingSnapshotAdapter::pollLatestSnapshot);
}

void BearingSnapshotAdapter::start()
{
    if (!m_scanController || !m_dataIngestPipeline) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "BearingSnapshotAdapter start skipped: dependencies are not configured");
        return;
    }
    if (!m_pollTimer.isActive()) {
        m_pollTimer.start();
    }
}

void BearingSnapshotAdapter::stop()
{
    m_pollTimer.stop();
}

bool BearingSnapshotAdapter::running() const noexcept
{
    return m_pollTimer.isActive();
}

void BearingSnapshotAdapter::pollLatestSnapshot()
{
    if (!m_scanController || !m_dataIngestPipeline || !m_scanController->scanActive()) {
        return;
    }

    const auto snapshot = m_dataIngestPipeline->latestBearingSnapshot();
    if (!snapshot || snapshot->sequenceId == m_lastSnapshotSequenceId) {
        return;
    }

    m_lastSnapshotSequenceId = snapshot->sequenceId;
    m_scanController->acceptBearingSnapshotSummary(snapshot);
}

void BearingSnapshotAdapter::publish(infrastructure::DiagnosticSeverity severity,
                                     const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "BearingSnapshotAdapter",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::app
