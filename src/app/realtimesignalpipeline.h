#pragma once

#include "processing/sample_processor.h"
#include "waterfallrenderbufferadapter.h"

#include <QtGlobal>

#include <cstddef>
#include <optional>

namespace siriusscope::infrastructure {
class IDiagnosticsSink;
}

namespace siriusscope::app {

class BearingFrameBus;
class SignalSampleBus;

struct RealtimeSignalPipelineConfig
{
    processing::SampleProcessingConfig processingConfig;
    SignalSampleBus* signalSampleBus = nullptr;
    BearingFrameBus* bearingFrameBus = nullptr;
    double sourceMinHz = 300e6;
    double sourceMaxHz = 18e9;
    int renderBinCount = 1024;
    infrastructure::IDiagnosticsSink* diagnosticsSink = nullptr;
};

struct RealtimeSignalPipelineInput
{
    processing::SampleBatch batch;
    qint64 utcMs = 0;
};

struct RealtimeSignalPipelineResult
{
    processing::SampleProcessingResult processingResult;
    std::optional<WaterfallRenderBufferAdapterResult> renderResult;
    std::size_t inputSampleCount = 0;
    std::size_t emptyBatchCount = 0;
};

class RealtimeSignalPipeline
{
public:
    explicit RealtimeSignalPipeline(RealtimeSignalPipelineConfig config = {});

    void setProcessingConfig(processing::SampleProcessingConfig config);
    void setSignalSampleBus(SignalSampleBus* bus) noexcept;
    void setBearingFrameBus(BearingFrameBus* bus) noexcept;
    void setWaterfallRenderContext(double sourceMinHz,
                                   double sourceMaxHz,
                                   int renderBinCount) noexcept;
    void setDiagnosticsSink(infrastructure::IDiagnosticsSink* sink) noexcept;
    RealtimeSignalPipelineResult process(RealtimeSignalPipelineInput input);

private:
    void publishProcessingDiagnostics(
        const std::vector<processing::ProcessingDiagnostic>& diagnostics) const;

    processing::SampleProcessingConfig m_processingConfig;
    processing::SampleProcessor m_processor;
    SignalSampleBus* m_signalSampleBus = nullptr;
    BearingFrameBus* m_bearingFrameBus = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    double m_sourceMinHz = 300e6;
    double m_sourceMaxHz = 18e9;
    int m_renderBinCount = 1024;
};

} // namespace siriusscope::app
