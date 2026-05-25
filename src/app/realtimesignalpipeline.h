#pragma once

#include "processing/sample_processor.h"

#include <cstddef>

namespace siriusscope::app {

class BearingFrameBus;
class SignalSampleBus;

struct RealtimeSignalPipelineConfig
{
    processing::SampleProcessingConfig processingConfig;
    SignalSampleBus* signalSampleBus = nullptr;
    BearingFrameBus* bearingFrameBus = nullptr;
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
    void setSignalSampleBus(SignalSampleBus* bus) noexcept;
    void setBearingFrameBus(BearingFrameBus* bus) noexcept;
    RealtimeSignalPipelineResult process(RealtimeSignalPipelineInput input);

private:
    processing::SampleProcessingConfig m_processingConfig;
    processing::SampleProcessor m_processor;
    SignalSampleBus* m_signalSampleBus = nullptr;
    BearingFrameBus* m_bearingFrameBus = nullptr;
};

} // namespace siriusscope::app
