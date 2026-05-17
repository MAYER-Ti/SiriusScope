#include "nullresulttablesink.h"

namespace siriusscope::app {

core::OperationResult NullResultTableSink::appendBearingResults(
    const ResultTableAppendContext&,
    const std::vector<core::BearingResult>&)
{
    return core::OperationResult::ok();
}

} // namespace siriusscope::app
