#include "hardware/simulator/high_load_simulator_bco_control.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <utility>

namespace siriusscope::hardware {

namespace {

std::string validationMessage(const core::ValidationResult& validation)
{
    std::string message;
    for (const auto& issue : validation.issues()) {
        if (!message.empty()) {
            message += "; ";
        }
        message += issue.message.empty() ? "domain validation error" : issue.message;
    }

    return message.empty() ? "invalid BCO command" : message;
}

} // namespace

HighLoadSimulatorBcoControl::HighLoadSimulatorBcoControl(
    HardwareProfile* profile,
    IBcoStreamSource* streamSource,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_profile(profile)
    , m_streamSource(streamSource)
    , m_diagnosticsSink(diagnosticsSink)
{
}

core::OperationResult HighLoadSimulatorBcoControl::applyBandConfig(
    const core::BandConfig& config)
{
    if (!m_profile) {
        const auto message = "high-load BCO control rejected band config: profile is not available";
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    const auto validation = config.validate();
    if (!validation) {
        const auto message = "invalid BandConfig: " + validationMessage(validation);
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    auto configs = m_profile->bcoStreamConfig.bandConfigs;
    const auto found = std::find_if(configs.begin(), configs.end(), [&config](const auto& current) {
        return current.bandIndex == config.bandIndex;
    });
    if (found == configs.end()) {
        const auto message = "BandConfig rejected: bandIndex is not configured";
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    *found = config;
    m_profile->bcoStreamConfig.bandConfigs = std::move(configs);

    publish(infrastructure::DiagnosticSeverity::Info,
            "high-load BCO simulator applied BandConfig for band "
                + std::to_string(config.bandIndex));
    return core::OperationResult::ok();
}

core::OperationResult HighLoadSimulatorBcoControl::applyBandConfigs(
    const std::vector<core::BandConfig>& configs)
{
    if (!m_profile) {
        const auto message = "high-load BCO control rejected band configs: profile is not available";
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    const auto validation = validateBandConfigs(configs);
    if (!validation) {
        publish(infrastructure::DiagnosticSeverity::Error, validation.message);
        return validation;
    }

    m_profile->bcoStreamConfig.bandConfigs = configs;

    publish(infrastructure::DiagnosticSeverity::Info,
            "high-load BCO simulator applied " + std::to_string(configs.size())
                + " BandConfig records");
    return core::OperationResult::ok();
}

core::OperationResult HighLoadSimulatorBcoControl::startProcessing(
    const BcoProcessingStartCommand& command)
{
    m_processingState = BcoProcessingState::Starting;

    if (!m_profile) {
        const auto message = "high-load BCO control start rejected: profile is not available";
        publish(infrastructure::DiagnosticSeverity::Error, message);
        m_processingState = BcoProcessingState::Failed;
        return core::OperationResult::failure(message);
    }
    if (!m_streamSource) {
        const auto message = "high-load BCO control start rejected: stream source is not available";
        publish(infrastructure::DiagnosticSeverity::Error, message);
        m_processingState = BcoProcessingState::Failed;
        return core::OperationResult::failure(message);
    }

    const auto bandValidation = validateBandConfigs(command.bandConfigs);
    if (!bandValidation) {
        publish(infrastructure::DiagnosticSeverity::Error, bandValidation.message);
        m_processingState = BcoProcessingState::Failed;
        return bandValidation;
    }

    const auto timeValidation = command.timeBase.validate();
    if (!timeValidation) {
        const auto message = "invalid BCO processing TimeBase: "
            + validationMessage(timeValidation);
        publish(infrastructure::DiagnosticSeverity::Error, message);
        m_processingState = BcoProcessingState::Failed;
        return core::OperationResult::failure(message);
    }

    m_lastStartCommand = command;
    m_profile->bcoStreamConfig.bandConfigs = command.bandConfigs;
    m_profile->bcoStreamConfig.timeBase = command.timeBase;
    m_profile->bcoStreamConfig.sessionId = command.sessionId;

    const auto configured = m_streamSource->configure(m_profile->bcoStreamConfig);
    if (!configured) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "high-load BCO stream source configure failed: " + configured.message);
        m_processingState = BcoProcessingState::Failed;
        return configured;
    }

    m_processingState = BcoProcessingState::Active;
    publish(infrastructure::DiagnosticSeverity::Info,
            "high-load BCO processing started for session "
                + std::to_string(command.sessionId));
    return core::OperationResult::ok();
}

core::OperationResult HighLoadSimulatorBcoControl::stopProcessing()
{
    if (m_processingState == BcoProcessingState::Idle) {
        return core::OperationResult::ok();
    }

    m_processingState = BcoProcessingState::Stopping;
    m_processingState = BcoProcessingState::Idle;
    publish(infrastructure::DiagnosticSeverity::Info,
            "high-load BCO processing stopped");
    return core::OperationResult::ok();
}

core::OperationResult HighLoadSimulatorBcoControl::validateBandConfigs(
    const std::vector<core::BandConfig>& configs) const
{
    if (configs.empty()) {
        return core::OperationResult::failure("BandConfig set must not be empty");
    }

    std::set<int> seenBandIndexes;
    for (const auto& config : configs) {
        const auto validation = config.validate();
        if (!validation) {
            return core::OperationResult::failure(
                "invalid BandConfig: " + validationMessage(validation));
        }

        if (!seenBandIndexes.insert(config.bandIndex).second) {
            return core::OperationResult::failure(
                "BandConfig set contains duplicate bandIndex "
                + std::to_string(config.bandIndex));
        }
    }

    return core::OperationResult::ok();
}

void HighLoadSimulatorBcoControl::publish(infrastructure::DiagnosticSeverity severity,
                                          const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "HighLoadSimulatorBcoControl",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::hardware
