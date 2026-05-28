#include "pipeline/pipeline_diagnostics.h"

#include <algorithm>
#include <sstream>

namespace siriusscope::pipeline {

bool PipelineDiagnosticCounters::empty() const noexcept
{
    return incompleteBearingCandidates == 0 && missingBeam0 == 0 && missingBeam1 == 0
        && droppedBlocks == 0 && droppedSamples == 0 && queueOverflows == 0
        && processingLatencyMaxMs <= 0.0;
}

PipelineDiagnostics::PipelineDiagnostics(PipelineDiagnosticsConfig config)
    : m_config(std::move(config))
{
    if (m_config.publishInterval.count() < 0) {
        m_config.publishInterval = std::chrono::milliseconds{0};
    }
    m_lastPublishedAt = std::chrono::steady_clock::now() - m_config.publishInterval;
}

void PipelineDiagnostics::recordIncompleteBearingCandidate()
{
    std::lock_guard lock(m_mutex);
    ++m_counters.incompleteBearingCandidates;
}

void PipelineDiagnostics::recordMissingBeam(int beamIndex)
{
    std::lock_guard lock(m_mutex);
    if (beamIndex == 0) {
        ++m_counters.missingBeam0;
    } else if (beamIndex == 1) {
        ++m_counters.missingBeam1;
    }
}

void PipelineDiagnostics::recordDroppedBlock(std::uint64_t sampleCount)
{
    std::lock_guard lock(m_mutex);
    ++m_counters.droppedBlocks;
    m_counters.droppedSamples += sampleCount;
}

void PipelineDiagnostics::recordQueueOverflow()
{
    std::lock_guard lock(m_mutex);
    ++m_counters.queueOverflows;
}

void PipelineDiagnostics::recordProcessingLatency(std::chrono::milliseconds latency)
{
    std::lock_guard lock(m_mutex);
    m_counters.processingLatencyMaxMs =
        std::max(m_counters.processingLatencyMaxMs, static_cast<double>(latency.count()));
}

PipelineDiagnosticCounters PipelineDiagnostics::counters() const
{
    std::lock_guard lock(m_mutex);
    return m_counters;
}

bool PipelineDiagnostics::publishIfDue(infrastructure::IDiagnosticsSink* sink, bool force)
{
    if (!sink) {
        return false;
    }

    PipelineDiagnosticCounters counters;
    {
        std::lock_guard lock(m_mutex);
        if (m_counters.empty()) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        if (!force && now - m_lastPublishedAt < m_config.publishInterval) {
            return false;
        }

        counters = m_counters;
        m_counters = {};
        m_lastPublishedAt = now;
    }

    sink->publish(infrastructure::DiagnosticEvent{
        infrastructure::DiagnosticSeverity::Warning,
        m_config.subsystem,
        buildMessage(counters),
        std::chrono::system_clock::now(),
    });
    return true;
}

std::string PipelineDiagnostics::buildMessage(
    const PipelineDiagnosticCounters& counters) const
{
    std::ostringstream stream;
    stream << "data plane diagnostics:";
    if (counters.incompleteBearingCandidates > 0) {
        stream << " incompleteBearingCandidates=" << counters.incompleteBearingCandidates;
    }
    if (counters.missingBeam0 > 0) {
        stream << " missingBeam0=" << counters.missingBeam0;
    }
    if (counters.missingBeam1 > 0) {
        stream << " missingBeam1=" << counters.missingBeam1;
    }
    if (counters.droppedBlocks > 0) {
        stream << " droppedBlocks=" << counters.droppedBlocks;
    }
    if (counters.droppedSamples > 0) {
        stream << " droppedSamples=" << counters.droppedSamples;
    }
    if (counters.queueOverflows > 0) {
        stream << " queueOverflows=" << counters.queueOverflows;
    }
    if (counters.processingLatencyMaxMs > 0.0) {
        stream << " processingLatencyMaxMs=" << counters.processingLatencyMaxMs;
    }
    return stream.str();
}

} // namespace siriusscope::pipeline
