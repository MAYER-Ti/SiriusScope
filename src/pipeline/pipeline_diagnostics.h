#pragma once

#include "infrastructure/interfaces/diagnostics_sink.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace siriusscope::pipeline {

struct PipelineDiagnosticsConfig
{
    std::chrono::milliseconds publishInterval{1000};
    std::string subsystem = "DataIngestPipeline";
};

struct PipelineDiagnosticCounters
{
    std::uint64_t incompleteBearingCandidates = 0;
    std::uint64_t missingBeam0 = 0;
    std::uint64_t missingBeam1 = 0;
    std::uint64_t droppedBlocks = 0;
    std::uint64_t droppedSamples = 0;
    std::uint64_t queueOverflows = 0;
    double processingLatencyMaxMs = 0.0;

    bool empty() const noexcept;
};

class PipelineDiagnostics
{
public:
    explicit PipelineDiagnostics(PipelineDiagnosticsConfig config = {});

    void recordIncompleteBearingCandidate();
    void recordMissingBeam(int beamIndex);
    void recordDroppedBlock(std::uint64_t sampleCount);
    void recordQueueOverflow();
    void recordProcessingLatency(std::chrono::milliseconds latency);

    PipelineDiagnosticCounters counters() const;
    bool publishIfDue(infrastructure::IDiagnosticsSink* sink, bool force = false);

private:
    std::string buildMessage(const PipelineDiagnosticCounters& counters) const;

    PipelineDiagnosticsConfig m_config;
    mutable std::mutex m_mutex;
    PipelineDiagnosticCounters m_counters;
    std::chrono::steady_clock::time_point m_lastPublishedAt;
};

} // namespace siriusscope::pipeline
