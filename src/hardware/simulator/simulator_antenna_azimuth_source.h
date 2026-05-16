#pragma once

#include "hardware/interfaces/antenna_azimuth_source.h"
#include "hardware/simulator/simulator_antenna_state.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace siriusscope::hardware {

struct SimulatorAntennaAzimuthSourceConfig
{
    std::chrono::milliseconds updatePeriod{100};
    double initialAzimuthDeg = 0.0;
    double movementSpeedDegPerSecond = 60.0;
};

class SimulatorAntennaAzimuthSource final : public IAntennaAzimuthSource
{
public:
    explicit SimulatorAntennaAzimuthSource(
        SimulatorAntennaState* state,
        SimulatorAntennaAzimuthSourceConfig config = {},
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr);
    ~SimulatorAntennaAzimuthSource() override;

    core::OperationResult start(AzimuthCallback callback) override;
    core::OperationResult stop() override;

private:
    void updateAzimuth();
    void generationLoop(AzimuthCallback callback);
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    SimulatorAntennaState* m_state = nullptr;
    SimulatorAntennaAzimuthSourceConfig m_config;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::thread m_worker;
    bool m_running = false;
    bool m_stopRequested = false;
};

} // namespace siriusscope::hardware
