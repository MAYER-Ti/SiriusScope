#include "app/realtimesignalpipeline.h"

#include <utility>

namespace siriusscope::app {

RealtimeSignalPipeline::RealtimeSignalPipeline(RealtimeSignalPipelineConfig config)
    : m_processingConfig(std::move(config.processingConfig))
    , m_processor(m_processingConfig)
{
}

void RealtimeSignalPipeline::setProcessingConfig(processing::SampleProcessingConfig config)
{
    m_processingConfig = std::move(config);
    m_processor = processing::SampleProcessor(m_processingConfig);
}

RealtimeSignalPipelineResult RealtimeSignalPipeline::process(RealtimeSignalPipelineInput input)
{
    RealtimeSignalPipelineResult result;
    result.inputSampleCount = input.batch.samples.size();

    if (input.batch.samples.empty()) {
        result.emptyBatchCount = 1;
        return result;
    }

    result.processingResult = m_processor.processBatch(input.batch);
    return result;
}

} // namespace siriusscope::app
