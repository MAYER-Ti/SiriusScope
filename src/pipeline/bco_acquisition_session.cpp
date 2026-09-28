#include "pipeline/bco_acquisition_session.h"
#include "pipeline/data_ingest_pipeline.h"
#include <sstream>

namespace siriusscope::pipeline {

BcoAcquisitionSession::BcoAcquisitionSession(
    hardware::IBcoStreamSource* source, DataIngestPipeline* pipeline,
    infrastructure::IDiagnosticsSink* diagnostics, SourceToPipelineBridgeConfig config)
    : m_source(source), m_pipeline(pipeline), m_diagnostics(diagnostics),
      m_bridge(pipeline, config, diagnostics)
{
}

BcoAcquisitionSession::~BcoAcquisitionSession()
{
    setAccepting(false);
    stopSource();
    closeInput(false);
}

core::OperationResult BcoAcquisitionSession::setSource(hardware::IBcoStreamSource* source)
{
    if (m_active || m_bridge.running()) {
        return core::OperationResult::failure("stop source and close input before switching BCO source");
    }
    m_source = source;
    return core::OperationResult::ok();
}

core::OperationResult BcoAcquisitionSession::startSource()
{
    if (m_active) return core::OperationResult::ok();
    if (!m_source || !m_pipeline) {
        return core::OperationResult::failure("BCO acquisition source or pipeline is not configured");
    }
    const auto pipelineStarted = m_pipeline->start();
    if (!pipelineStarted) return pipelineStarted;
    const auto started = m_source->start([this](hardware::IBcoStreamSource::SampleBlockPtr block) {
        std::lock_guard lock(m_deliveryMutex);
        if (m_accepting) m_bridge.submit(std::move(block));
    });
    m_active = started.success;
    return started;
}

core::OperationResult BcoAcquisitionSession::stopSource()
{
    if (!m_active) return core::OperationResult::ok();
    const auto result = m_source->stop();
    if (result) m_active = false;
    return result;
}

core::OperationResult BcoAcquisitionSession::openInput()
{
    if (!m_pipeline) return core::OperationResult::failure("BCO acquisition pipeline is not configured");
    return m_bridge.start();
}

void BcoAcquisitionSession::setAccepting(bool accepting)
{
    std::lock_guard lock(m_deliveryMutex);
    m_accepting = accepting && m_bridge.running();
}

void BcoAcquisitionSession::closeInput(bool drain)
{
    setAccepting(false);
    if (!m_bridge.running()) return;
    if (drain) {
        const auto result = m_bridge.flush(std::chrono::seconds{5});
        if (!result) publish(infrastructure::DiagnosticSeverity::Warning,
                             "Source bridge flush timed out: " + result.message);
    }
    m_bridge.stop();
    const auto snapshot = m_bridge.metrics();
    if (snapshot.receivedBlocks == 0) return;
    std::ostringstream summary;
    summary << "Source bridge stopped: received=" << snapshot.receivedBlocks
            << " enqueued=" << snapshot.enqueuedBlocks << " dropped=" << snapshot.droppedBlocks
            << " ingested=" << snapshot.ingestedBlocks << " rejected=" << snapshot.rejectedBlocks
            << " queueDepth=" << snapshot.queueDepth;
    publish(infrastructure::DiagnosticSeverity::Info, summary.str());
    if (snapshot.droppedBlocks || snapshot.rejectedBlocks) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "Source bridge reported dropped/rejected blocks: " + summary.str());
    }
}

void BcoAcquisitionSession::clearInput()
{
    std::lock_guard lock(m_deliveryMutex);
    m_bridge.clear();
}

core::OperationResult BcoAcquisitionSession::flushInput(std::chrono::milliseconds timeout)
{
    return m_bridge.flush(timeout);
}

SourceToPipelineBridgeMetrics BcoAcquisitionSession::metrics() const
{
    return m_bridge.metrics();
}

void BcoAcquisitionSession::publish(infrastructure::DiagnosticSeverity severity,
                                   const std::string& message) const
{
    if (m_diagnostics) m_diagnostics->publish({severity, "BcoAcquisitionSession", message,
                                             std::chrono::system_clock::now()});
}

} // namespace siriusscope::pipeline
