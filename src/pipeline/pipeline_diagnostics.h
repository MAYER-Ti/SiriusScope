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
    std::uint64_t invalidFrequencySamples = 0;
    std::uint64_t outOfRangeSamples = 0;
    std::uint64_t emptyBlocks = 0;
    std::uint64_t producedRows = 0;
    std::uint64_t producedSnapshots = 0;
    double aggregationLatencyMaxMs = 0.0;
    std::uint64_t spectrumInvalidSamples = 0;
    std::uint64_t spectrumOutOfRangeSamples = 0;
    std::uint64_t producedSpectrumSnapshots = 0;
    double spectrumAggregationLatencyMaxMs = 0.0;
    std::uint64_t completeBearingCandidates = 0;
    std::uint64_t missingBeam0Candidates = 0;
    std::uint64_t missingBeam1Candidates = 0;
    std::uint64_t bearingSnapshotsProduced = 0;
    std::uint64_t bearingEstimatesProduced = 0;
    double bearingAggregationLatencyMaxMs = 0.0;

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
    void recordWaterfallAggregation(std::uint64_t invalidFrequencySamples,
                                    std::uint64_t outOfRangeSamples,
                                    std::uint64_t emptyBlocks,
                                    std::uint64_t producedRows,
                                    std::uint64_t producedSnapshots,
                                    std::chrono::milliseconds aggregationLatency);
    void recordSpectrumAggregation(std::uint64_t invalidSamples,
                                   std::uint64_t outOfRangeSamples,
                                   std::uint64_t producedSnapshots,
                                   std::chrono::milliseconds aggregationLatency);
    void recordBearingAggregation(std::uint64_t completeCandidates,
                                  std::uint64_t incompleteCandidates,
                                  std::uint64_t missingBeam0Candidates,
                                  std::uint64_t missingBeam1Candidates,
                                  std::uint64_t producedSnapshots,
                                  std::uint64_t producedEstimates,
                                  std::chrono::milliseconds aggregationLatency);

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
