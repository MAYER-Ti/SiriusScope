#pragma once

#include "app/interfaces/result_table_sink.h"

namespace siriusscope::app {

class NullResultTableSink final : public IResultTableSink
{
public:
    core::OperationResult appendBearingResults(
        const ResultTableAppendContext& context,
        const std::vector<core::BearingResult>& results) override;
};

} // namespace siriusscope::app
