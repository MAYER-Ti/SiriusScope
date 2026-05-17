#pragma once

#include "core/operation_result.h"

#include <chrono>

namespace siriusscope::app {

class IProcessingFlushControl
{
public:
    virtual ~IProcessingFlushControl() = default;

    virtual core::OperationResult flushProcessing(std::chrono::milliseconds timeout) = 0;
};

} // namespace siriusscope::app
