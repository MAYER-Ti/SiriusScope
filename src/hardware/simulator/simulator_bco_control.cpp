#include "hardware/simulator/simulator_bco_control.h"

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

    return message.empty() ? "invalid band configuration" : message;
}

} // namespace

SimulatorBcoControl::SimulatorBcoControl(SimulatorBcoSampleSource* sampleSource,
                                         infrastructure::IDiagnosticsSink* diagnosticsSink)
    : m_sampleSource(sampleSource)
    , m_diagnosticsSink(diagnosticsSink)
{
}

core::OperationResult SimulatorBcoControl::applyBandConfig(const core::BandConfig& config)
{
    if (!m_sampleSource) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "BCO simulator control rejected band config: sample source is not available");
        return core::OperationResult::failure("BCO simulator sample source is not available");
    }

    const auto validation = config.validate();
    if (!validation) {
        const auto message = "invalid BandConfig: " + validationMessage(validation);
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    auto configs = m_sampleSource->bandConfigs();
    const auto found = std::find_if(configs.begin(), configs.end(), [&config](const auto& current) {
        return current.bandIndex == config.bandIndex;
    });

    if (found == configs.end()) {
        const auto message = "BandConfig rejected: bandIndex is not configured";
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    *found = config;
    m_sampleSource->setBandConfigs(std::move(configs));

    publish(infrastructure::DiagnosticSeverity::Info,
            "BCO simulator applied BandConfig for band " + std::to_string(config.bandIndex));
    return core::OperationResult::ok();
}

core::OperationResult SimulatorBcoControl::applyBandConfigs(
    const std::vector<core::BandConfig>& configs)
{
    if (!m_sampleSource) {
        publish(infrastructure::DiagnosticSeverity::Error,
                "BCO simulator control rejected band configs: sample source is not available");
        return core::OperationResult::failure("BCO simulator sample source is not available");
    }

    if (configs.empty()) {
        const auto message = "BandConfig set must not be empty";
        publish(infrastructure::DiagnosticSeverity::Error, message);
        return core::OperationResult::failure(message);
    }

    std::set<int> seenBandIndexes;
    for (const auto& config : configs) {
        const auto validation = config.validate();
        if (!validation) {
            const auto message = "invalid BandConfig: " + validationMessage(validation);
            publish(infrastructure::DiagnosticSeverity::Error, message);
            return core::OperationResult::failure(message);
        }

        if (!seenBandIndexes.insert(config.bandIndex).second) {
            const auto message =
                "BandConfig set contains duplicate bandIndex " + std::to_string(config.bandIndex);
            publish(infrastructure::DiagnosticSeverity::Error, message);
            return core::OperationResult::failure(message);
        }
    }

    m_sampleSource->setBandConfigs(configs);

    publish(infrastructure::DiagnosticSeverity::Info,
            "BCO simulator applied " + std::to_string(configs.size()) + " BandConfig records");
    return core::OperationResult::ok();
}

void SimulatorBcoControl::publish(infrastructure::DiagnosticSeverity severity,
                                  const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SimulatorBcoControl",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::hardware
