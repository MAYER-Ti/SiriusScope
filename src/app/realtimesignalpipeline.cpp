#include "app/realtimesignalpipeline.h"

#include "app/bearingframebus.h"
#include "app/signalsamplebus.h"

#include <utility>

namespace siriusscope::app {

RealtimeSignalPipeline::RealtimeSignalPipeline(RealtimeSignalPipelineConfig config)
    : m_processingConfig(std::move(config.processingConfig))
    , m_processor(m_processingConfig)
    , m_signalSampleBus(config.signalSampleBus)
    , m_bearingFrameBus(config.bearingFrameBus)
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

    return result;
}

} // namespace siriusscope::app
