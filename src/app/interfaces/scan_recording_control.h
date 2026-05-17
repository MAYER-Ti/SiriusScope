#pragma once

#include "core/operation_result.h"

#include <cstdint>

namespace siriusscope::app {

class IScanRecordingControl
{
public:
    virtual ~IScanRecordingControl() = default;

    virtual core::OperationResult beginScanRecording(std::uint64_t scanSessionId) = 0;
    virtual core::OperationResult endScanRecording(std::uint64_t scanSessionId) = 0;
};

} // namespace siriusscope::app
