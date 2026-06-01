#pragma once

#include "core/operation_result.h"

#include <chrono>
#include <functional>
#include <memory>

namespace siriusscope::pipeline {
struct SignalParameterSnapshot;
}

namespace siriusscope::app {

class IProcessingFlushControl
{
public:
    using FlushCallback = std::function<void(core::OperationResult)>;

    virtual ~IProcessingFlushControl() = default;

    virtual core::OperationResult flushProcessing(std::chrono::milliseconds timeout) = 0;
    virtual void flushProcessingAsync(std::chrono::milliseconds timeout,
                                      FlushCallback callback) = 0;
    virtual std::shared_ptr<const pipeline::SignalParameterSnapshot>
    latestSignalParameterSnapshot() const = 0;
};

} // namespace siriusscope::app
