#include "app/realtimesignalpipeline.h"

#include "app/bearingframebus.h"
#include "app/signalsamplebus.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <utility>

namespace siriusscope::app {
namespace {

std::string severityName(processing::ProcessingDiagnosticSeverity severity)
{
    switch (severity) {
    case processing::ProcessingDiagnosticSeverity::Info:
        return "info";
    case processing::ProcessingDiagnosticSeverity::Warning:
        return "warning";
    case processing::ProcessingDiagnosticSeverity::Error:
        return "error";
    }
    return "unknown";
}

infrastructure::DiagnosticSeverity mapSeverity(processing::ProcessingDiagnosticSeverity severity)
{
    switch (severity) {
    case processing::ProcessingDiagnosticSeverity::Info:
        return infrastructure::DiagnosticSeverity::Info;
    case processing::ProcessingDiagnosticSeverity::Warning:
        return infrastructure::DiagnosticSeverity::Warning;
    case processing::ProcessingDiagnosticSeverity::Error:
        return infrastructure::DiagnosticSeverity::Error;
    }
    return infrastructure::DiagnosticSeverity::Warning;
}

std::string domainIssueName(core::ValidationCode code)
{
    return std::to_string(static_cast<int>(code));
}

std::string processingDiagnosticMessage(
    const processing::ProcessingDiagnostic& diagnostic)
{
    std::ostringstream message;
    message << diagnostic.message
            << " [processingCode=" << static_cast<int>(diagnostic.code)
            << ", severity=" << severityName(diagnostic.severity);

    if (diagnostic.sampleIndex) {
        message << ", sampleIndex=" << *diagnostic.sampleIndex;
    }
    if (diagnostic.bandIndex) {
        message << ", bandIndex=" << *diagnostic.bandIndex;
    }
    if (diagnostic.beamIndex) {
        message << ", beamIndex=" << *diagnostic.beamIndex;
    }
    if (diagnostic.frequencyHz) {
        message << ", frequencyHz=" << *diagnostic.frequencyHz;
    }
    if (!diagnostic.domainIssues.empty()) {
        message << ", domainIssues=";
        for (std::size_t i = 0; i < diagnostic.domainIssues.size(); ++i) {
            if (i > 0) {
                message << '|';
            }
            message << domainIssueName(diagnostic.domainIssues[i].code);
        }
    }

    message << ']';
    return message.str();
}

} // namespace

RealtimeSignalPipeline::RealtimeSignalPipeline(RealtimeSignalPipelineConfig config)
    : m_processingConfig(std::move(config.processingConfig))
    , m_processor(m_processingConfig)
    , m_signalSampleBus(config.signalSampleBus)
    , m_bearingFrameBus(config.bearingFrameBus)
    , m_diagnosticsSink(config.diagnosticsSink)
    , m_sourceMinHz(config.sourceMinHz)
    , m_sourceMaxHz(config.sourceMaxHz)
    , m_renderBinCount(std::max(1, config.renderBinCount))
{
}

void RealtimeSignalPipeline::setProcessingConfig(processing::SampleProcessingConfig config)
{
    m_processingConfig = std::move(config);
    m_processor = processing::SampleProcessor(m_processingConfig);
}

void RealtimeSignalPipeline::setSignalSampleBus(SignalSampleBus* bus) noexcept
{
    m_signalSampleBus = bus;
}

void RealtimeSignalPipeline::setBearingFrameBus(BearingFrameBus* bus) noexcept
{
    m_bearingFrameBus = bus;
}

void RealtimeSignalPipeline::setWaterfallRenderContext(double sourceMinHz,
                                                       double sourceMaxHz,
                                                       int renderBinCount) noexcept
{
    m_sourceMinHz = sourceMinHz;
    m_sourceMaxHz = sourceMaxHz;
    m_renderBinCount = std::max(1, renderBinCount);
}

void RealtimeSignalPipeline::setDiagnosticsSink(
    infrastructure::IDiagnosticsSink* sink) noexcept
{
    m_diagnosticsSink = sink;
}

RealtimeSignalPipelineResult RealtimeSignalPipeline::process(RealtimeSignalPipelineInput input)
{
    RealtimeSignalPipelineResult result;
    result.inputSampleCount = input.batch.samples.size();

    if (input.batch.samples.empty()) {
        result.emptyBatchCount = 1;
        return result;
    }

    if (m_signalSampleBus) {
        m_signalSampleBus->publish(input.batch.samples);
    }

    result.processingResult = m_processor.processBatch(input.batch);
    publishProcessingDiagnostics(result.processingResult.diagnostics);

    if (m_bearingFrameBus && !result.processingResult.bearingFrames.empty()) {
        m_bearingFrameBus->publish(result.processingResult.bearingFrames);
    }

    if (!result.processingResult.waterfallFrame.rows.empty()) {
        result.renderResult = WaterfallRenderBufferAdapter::adaptFrame(
            result.processingResult.waterfallFrame,
            input.utcMs,
            m_sourceMinHz,
            m_sourceMaxHz,
            m_renderBinCount);
    }

    return result;
}

void RealtimeSignalPipeline::publishProcessingDiagnostics(
    const std::vector<processing::ProcessingDiagnostic>& diagnostics) const
{
    if (!m_diagnosticsSink || diagnostics.empty()) {
        return;
    }

    for (const auto& diagnostic : diagnostics) {
        m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
            mapSeverity(diagnostic.severity),
            "RealtimeSignalPipeline",
            processingDiagnosticMessage(diagnostic),
            std::chrono::system_clock::now(),
        });
    }
}

} // namespace siriusscope::app
