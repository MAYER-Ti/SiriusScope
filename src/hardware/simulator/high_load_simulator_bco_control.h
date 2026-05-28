#pragma once

#include "hardware/hardware_profile.h"
#include "hardware/interfaces/bco_control.h"
#include "hardware/interfaces/bco_stream_source.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <optional>
#include <string>
#include <vector>

namespace siriusscope::hardware {

class HighLoadSimulatorBcoControl final : public IBcoControl
{
public:
    explicit HighLoadSimulatorBcoControl(
        HardwareProfile* profile,
        IBcoStreamSource* streamSource,
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr);

    core::OperationResult applyBandConfig(const core::BandConfig& config) override;
    core::OperationResult applyBandConfigs(const std::vector<core::BandConfig>& configs) override;
    core::OperationResult startProcessing(const BcoProcessingStartCommand& command) override;
    core::OperationResult stopProcessing() override;

    BcoProcessingState processingState() const noexcept { return m_processingState; }
    const std::optional<BcoProcessingStartCommand>& lastStartCommand() const noexcept
    {
        return m_lastStartCommand;
    }

private:
    core::OperationResult validateBandConfigs(
        const std::vector<core::BandConfig>& configs) const;
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    HardwareProfile* m_profile = nullptr;
    IBcoStreamSource* m_streamSource = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    BcoProcessingState m_processingState = BcoProcessingState::Idle;
    std::optional<BcoProcessingStartCommand> m_lastStartCommand;
};

} // namespace siriusscope::hardware
