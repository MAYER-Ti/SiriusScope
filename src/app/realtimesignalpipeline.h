#pragma once

#include "processing/sample_processor.h"

#include <cstddef>

namespace siriusscope::app {

struct RealtimeSignalPipelineConfig
{
    processing::SampleProcessingConfig processingConfig;
};

struct RealtimeSignalPipelineInput
{
    processing::SampleBatch batch;
};

struct RealtimeSignalPipelineResult
{
    processing::SampleProcessingResult processingResult;
    std::size_t inputSampleCount = 0;
    std::size_t emptyBatchCount = 0;
};

class RealtimeSignalPipeline
{
public:
    explicit RealtimeSignalPipeline(RealtimeSignalPipelineConfig config = {});

    void setProcessingConfig(processing::SampleProcessingConfig config);
    RealtimeSignalPipelineResult process(RealtimeSignalPipelineInput input);

private:
    processing::SampleProcessingConfig m_processingConfig;
    processing::SampleProcessor m_processor;
};

} // namespace siriusscope::app
