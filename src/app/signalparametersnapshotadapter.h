#pragma once

#include "infrastructure/interfaces/diagnostics_sink.h"
#include "pipeline/signal_parameter_snapshot.h"

#include <QObject>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <string>

namespace siriusscope::pipeline {
class DataIngestPipeline;
}

namespace siriusscope::app {

class ScanController;

struct SignalParameterSnapshotAdapterConfig
{
    int pollIntervalMs = 50;
};

class SignalParameterSnapshotAdapter final : public QObject
{
    Q_OBJECT

public:
    explicit SignalParameterSnapshotAdapter(
        ScanController* scanController,
        pipeline::DataIngestPipeline* dataIngestPipeline,
        infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr,
        SignalParameterSnapshotAdapterConfig config = {},
        QObject* parent = nullptr);

    void start();
    void stop();
    bool running() const noexcept;
    std::uint64_t lastSnapshotSequenceId() const noexcept
    {
        return m_lastSnapshotSequenceId;
    }

private slots:
    void pollLatestSnapshot();

private:
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    ScanController* m_scanController = nullptr;
    pipeline::DataIngestPipeline* m_dataIngestPipeline = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    SignalParameterSnapshotAdapterConfig m_config;
    QTimer m_pollTimer;
    std::uint64_t m_lastSnapshotSequenceId = 0;
};

} // namespace siriusscope::app
