#pragma once

#include "app/interfaces/result_table_sink.h"

namespace siriusscope::app {

class NullResultTableSink final : public IResultTableSink
{
public:
    core::OperationResult appendBearingResults(
        std::uint64_t scanSessionId,
        const std::vector<core::BearingResult>& results) override;
};

} // namespace siriusscope::app
