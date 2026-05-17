#include "nullresulttablesink.h"

namespace siriusscope::app {

core::OperationResult NullResultTableSink::appendBearingResults(
    std::uint64_t,
    const std::vector<core::BearingResult>&)
{
    return core::OperationResult::ok();
}

} // namespace siriusscope::app
