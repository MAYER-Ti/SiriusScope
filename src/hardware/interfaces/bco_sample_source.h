#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"

#include <functional>
#include <vector>

namespace siriusscope::hardware {

struct BcoSampleBatch
{
    std::vector<core::SignalSample> samples;
};

class IBcoSampleSource
{
public:
    using SampleBatchCallback = std::function<void(const BcoSampleBatch&)>;

    virtual ~IBcoSampleSource() = default;

    virtual core::OperationResult start(SampleBatchCallback callback) = 0;
    virtual core::OperationResult stop() = 0;
};

} // namespace siriusscope::hardware
