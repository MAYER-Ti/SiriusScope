#pragma once

#include "hardware/interfaces/bco_sample_source.h"
#include "hardware/simulator/simulator_radio_scene.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace siriusscope::hardware {

class SimulatorAntennaState;

struct SimulatorBcoSampleSourceConfig
{
    std::chrono::milliseconds batchPeriod{100};
    std::size_t samplesPerBatch = 128;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t sampleIndexStep = 1;
    // Values below this threshold are treated as no beam hit, not as BCO samples.
    int minVisibleAmplitude = 4;
};

class SimulatorBcoSampleSource final : public IBcoSampleSource
{
public:
    explicit SimulatorBcoSampleSource(
        SimulatorBcoSampleSourceConfig config = {},
        SimulatorAntennaState* antennaState = nullptr,
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr);
    ~SimulatorBcoSampleSource() override;

    core::OperationResult start(SampleBatchCallback callback) override;
    core::OperationResult stop() override;

    void setBandConfigs(std::vector<core::BandConfig> configs);
    std::vector<core::BandConfig> bandConfigs() const;
    void resetSession(std::uint64_t firstSampleIndex = 0);

private:
    BcoSampleBatch generateBatch();
    void generationLoop(SampleBatchCallback callback);
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    SimulatorBcoSampleSourceConfig m_config;
    SimulatorAntennaState* m_antennaState = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    SimulatorRadioScene m_scene;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    bool m_running = false;
    bool m_stopRequested = false;
    std::vector<core::BandConfig> m_bandConfigs;
    std::uint64_t m_nextSampleIndex = 0;
    std::uint64_t m_signalStep = 0;
};

} // namespace siriusscope::hardware
