#include "app/realtimesignalpipeline.h"

#include "app/bearingframebus.h"
#include "app/signalsamplebus.h"

#include <algorithm>
#include <utility>

namespace siriusscope::app {

RealtimeSignalPipeline::RealtimeSignalPipeline(RealtimeSignalPipelineConfig config)
    : m_processingConfig(std::move(config.processingConfig))
    , m_processor(m_processingConfig)
    , m_signalSampleBus(config.signalSampleBus)
    , m_bearingFrameBus(config.bearingFrameBus)
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

} // namespace siriusscope::app
