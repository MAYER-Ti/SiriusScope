#pragma once

#include "infrastructure/interfaces/diagnostics_sink.h"
#include "pipeline/spectrum_snapshot.h"

#include <QObject>
#include <QTimer>
#include <QVector>

#include <cstdint>
#include <memory>

class FrequencyViewportModel;

namespace siriusscope::pipeline {
class DataIngestPipeline;
}

namespace siriusscope::app {

class SpectrumEnvelopeController;

struct SpectrumSnapshotAdapterConfig
{
    int pollIntervalMs = 33;
    int outputBinCount = 1024;
};

class SpectrumSnapshotAdapter final : public QObject
{
    Q_OBJECT

public:
    explicit SpectrumSnapshotAdapter(FrequencyViewportModel* viewportModel,
                                     SpectrumEnvelopeController* envelopeController,
                                     pipeline::DataIngestPipeline* dataIngestPipeline,
                                     infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr,
                                     SpectrumSnapshotAdapterConfig config = {},
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
    void onViewportChanged(double minHz, double maxHz, const QString& sourceTag);

private:
    QVector<float> adaptSnapshot(const pipeline::SpectrumSnapshot& snapshot) const;
    void publishSnapshot(const std::shared_ptr<const pipeline::SpectrumSnapshot>& snapshot);
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    FrequencyViewportModel* m_viewportModel = nullptr;
    SpectrumEnvelopeController* m_envelopeController = nullptr;
    pipeline::DataIngestPipeline* m_dataIngestPipeline = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    SpectrumSnapshotAdapterConfig m_config;
    QTimer m_pollTimer;
    std::shared_ptr<const pipeline::SpectrumSnapshot> m_lastSnapshot;
    std::uint64_t m_lastSnapshotSequenceId = 0;
};

} // namespace siriusscope::app
