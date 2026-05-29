#pragma once

#include "hardware/hardware_profile.h"
#include "hardware/interfaces/antenna_azimuth_provider.h"
#include "hardware/simulator/simulator_radio_scene.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace siriusscope::hardware {

class HighLoadSimulatorBcoStreamSource final : public IBcoStreamSource
{
public:
    explicit HighLoadSimulatorBcoStreamSource(
        SimulatorBcoLoadConfig loadConfig = {},
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr,
        IAntennaAzimuthProvider* antennaAzimuthProvider = nullptr);

    ~HighLoadSimulatorBcoStreamSource() override;

    core::OperationResult configure(const BcoStreamConfig& config) override;
    core::OperationResult start(SampleBlockCallback callback) override;
    core::OperationResult stop() override;
    BcoSourceMetrics metrics() const override;

    void setPulseBandConfigs(std::vector<SimulatorPulseBandConfig> configs);
    std::vector<SimulatorPulseBandConfig> pulseBandConfigs() const;
    void setRadioScene(SimulatorRadioScene scene);
    SimulatorRadioScene radioScene() const;

private:
    void generationLoop(SampleBlockCallback callback);
    std::shared_ptr<const BcoSampleBlock> generateBlock(std::size_t sampleCount);
    std::size_t samplesPerBatchForCurrentPeriod(std::uint64_t batchIndex) const;
    void updateMetricsAfterBlock(const BcoSampleBlock& block,
                                 std::chrono::milliseconds callbackDuration);
    void publish(infrastructure::DiagnosticSeverity severity,
                 const std::string& message) const;

private:
    SimulatorBcoLoadConfig m_loadConfig;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    IAntennaAzimuthProvider* m_antennaAzimuthProvider = nullptr;
    SimulatorRadioScene m_scene;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_worker;

    BcoStreamConfig m_streamConfig;
    BcoSourceMetrics m_metrics;

    bool m_configured = false;
    bool m_running = false;
    bool m_stopRequested = false;

    std::uint64_t m_nextSampleIndex = 0;
    std::uint64_t m_generatedBatchIndex = 0;
    std::chrono::steady_clock::time_point m_startedAt{};
};

} // namespace siriusscope::hardware
