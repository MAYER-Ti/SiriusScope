#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"

#include <cstdint>
#include <vector>

namespace siriusscope::app {

class IResultTableSink
{
public:
    virtual ~IResultTableSink() = default;

    virtual core::OperationResult appendBearingResults(
        std::uint64_t scanSessionId,
        const std::vector<core::BearingResult>& results) = 0;
};

} // namespace siriusscope::app
