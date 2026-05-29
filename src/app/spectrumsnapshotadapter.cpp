#include "spectrumsnapshotadapter.h"

#include "frequencyviewportmodel.h"
#include "pipeline/data_ingest_pipeline.h"
#include "spectrumenvelopecontroller.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace siriusscope::app {
namespace {

SpectrumSnapshotAdapterConfig normalizeConfig(SpectrumSnapshotAdapterConfig config)
{
    config.pollIntervalMs = std::max(1, config.pollIntervalMs);
    config.outputBinCount = std::max(1, config.outputBinCount);
    return config;
}

int sourceBinForFrequency(const pipeline::SpectrumSnapshot& snapshot,
                          double frequencyHz)
{
    if (snapshot.renderBinCount <= 0 || snapshot.sourceMaxHz <= snapshot.sourceMinHz
        || !std::isfinite(frequencyHz) || frequencyHz < snapshot.sourceMinHz
        || frequencyHz > snapshot.sourceMaxHz) {
        return -1;
    }
    if (snapshot.renderBinCount == 1) {
        return 0;
    }

    const double ratio =
        (frequencyHz - static_cast<double>(snapshot.sourceMinHz))
        / static_cast<double>(snapshot.sourceMaxHz - snapshot.sourceMinHz);
    return std::clamp(static_cast<int>(std::lround(ratio * (snapshot.renderBinCount - 1))),
                      0,
                      snapshot.renderBinCount - 1);
}

} // namespace

SpectrumSnapshotAdapter::SpectrumSnapshotAdapter(
    FrequencyViewportModel* viewportModel,
    SpectrumEnvelopeController* envelopeController,
    pipeline::DataIngestPipeline* dataIngestPipeline,
    infrastructure::IDiagnosticsSink* diagnosticsSink,
    SpectrumSnapshotAdapterConfig config,
    QObject* parent)
    : QObject(parent)
    , m_viewportModel(viewportModel)
    , m_envelopeController(envelopeController)
    , m_dataIngestPipeline(dataIngestPipeline)
    , m_diagnosticsSink(diagnosticsSink)
    , m_config(normalizeConfig(config))
{
    m_pollTimer.setInterval(m_config.pollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &SpectrumSnapshotAdapter::pollLatestSnapshot);
    if (m_viewportModel) {
        connect(m_viewportModel,
                &FrequencyViewportModel::viewportChanged,
                this,
                &SpectrumSnapshotAdapter::onViewportChanged);
    }

    if (m_dataIngestPipeline && m_viewportModel) {
        pipeline::SpectrumAggregatorConfig spectrumConfig;
        spectrumConfig.renderBinCount = m_config.outputBinCount;
        spectrumConfig.sourceMinHz = static_cast<std::int64_t>(m_viewportModel->globalMinHz());
        spectrumConfig.sourceMaxHz = static_cast<std::int64_t>(m_viewportModel->globalMaxHz());
        m_dataIngestPipeline->configureSpectrum(spectrumConfig);
    }
}

void SpectrumSnapshotAdapter::start()
{
    if (!m_dataIngestPipeline || !m_envelopeController || !m_viewportModel) {
        publish(infrastructure::DiagnosticSeverity::Warning,
                "SpectrumSnapshotAdapter start skipped: dependencies are not configured");
        return;
    }
    if (!m_pollTimer.isActive()) {
        m_pollTimer.start();
    }
}

void SpectrumSnapshotAdapter::stop()
{
    m_pollTimer.stop();
}

bool SpectrumSnapshotAdapter::running() const noexcept
{
    return m_pollTimer.isActive();
}

void SpectrumSnapshotAdapter::pollLatestSnapshot()
{
    if (!m_dataIngestPipeline) {
        return;
    }

    const auto snapshot = m_dataIngestPipeline->latestSpectrumSnapshot();
    if (!snapshot) {
        return;
    }
    if (snapshot == m_lastSnapshot && snapshot->sequenceId == m_lastSnapshotSequenceId) {
        return;
    }

    publishSnapshot(snapshot);
}

void SpectrumSnapshotAdapter::onViewportChanged(double, double, const QString&)
{
    if (m_lastSnapshot) {
        publishSnapshot(m_lastSnapshot);
    }
}

QVector<float> SpectrumSnapshotAdapter::adaptSnapshot(
    const pipeline::SpectrumSnapshot& snapshot) const
{
    QVector<float> samples(m_config.outputBinCount, 0.0F);
    if (!m_viewportModel || snapshot.bins.empty() || snapshot.renderBinCount <= 0) {
        return samples;
    }

    const double viewMinHz = m_viewportModel->viewMinHz();
    const double viewMaxHz = m_viewportModel->viewMaxHz();
    if (!std::isfinite(viewMinHz) || !std::isfinite(viewMaxHz) || viewMaxHz <= viewMinHz) {
        return samples;
    }

    for (int index = 0; index < samples.size(); ++index) {
        const double ratio = samples.size() <= 1
            ? 0.0
            : static_cast<double>(index) / static_cast<double>(samples.size() - 1);
        const double frequencyHz = viewMinHz + ratio * (viewMaxHz - viewMinHz);
        const int sourceBin = sourceBinForFrequency(snapshot, frequencyHz);
        if (sourceBin < 0 || static_cast<std::size_t>(sourceBin) >= snapshot.bins.size()) {
            continue;
        }

        samples[index] =
            static_cast<float>(snapshot.bins[static_cast<std::size_t>(sourceBin)].totalPeak);
    }

    return samples;
}

void SpectrumSnapshotAdapter::publishSnapshot(
    const std::shared_ptr<const pipeline::SpectrumSnapshot>& snapshot)
{
    if (!snapshot || !m_envelopeController || !m_viewportModel) {
        return;
    }

    const auto samples = adaptSnapshot(*snapshot);
    m_envelopeController->acceptSnapshot(m_viewportModel->viewMinHz(),
                                         m_viewportModel->viewMaxHz(),
                                         samples);
    m_lastSnapshot = snapshot;
    m_lastSnapshotSequenceId = snapshot->sequenceId;
}

void SpectrumSnapshotAdapter::publish(infrastructure::DiagnosticSeverity severity,
                                      const std::string& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "SpectrumSnapshotAdapter",
        message,
        std::chrono::system_clock::now(),
    });
}

} // namespace siriusscope::app
