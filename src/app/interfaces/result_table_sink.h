#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"
#include "processing/signal_parameter_estimator.h"

#include <cstdint>
#include <vector>

namespace siriusscope::app {

struct ResultTableAppendContext
{
    std::uint64_t scanSessionId = 0;
    double antennaAzimuthDeg = 0.0;
    std::vector<processing::SignalParameters> signalParameters;
};

class IResultTableSink
{
public:
    virtual ~IResultTableSink() = default;

    virtual core::OperationResult appendBearingResults(
        const ResultTableAppendContext& context,
        const std::vector<core::BearingResult>& results) = 0;
};

} // namespace siriusscope::app
