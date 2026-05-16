#pragma once

#include <chrono>
#include <string>

namespace siriusscope::infrastructure {

enum class DiagnosticSeverity
{
    Info,
    Warning,
    Error
};

struct DiagnosticEvent
{
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string subsystem;
    std::string message;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::time_point{};
};

class IDiagnosticsSink
{
public:
    virtual ~IDiagnosticsSink() = default;

    virtual void publish(const DiagnosticEvent& event) = 0;
};

class NullDiagnosticsSink final : public IDiagnosticsSink
{
public:
    void publish(const DiagnosticEvent&) override {}
};

} // namespace siriusscope::infrastructure
