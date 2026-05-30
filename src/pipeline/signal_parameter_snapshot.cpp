#include "pipeline/signal_parameter_snapshot.h"

#include <chrono>

namespace siriusscope::pipeline {

std::int64_t currentSignalParameterSnapshotUtcNs()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

} // namespace siriusscope::pipeline
