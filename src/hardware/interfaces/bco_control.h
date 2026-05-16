#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"

#include <vector>

namespace siriusscope::hardware {

class IBcoControl
{
public:
    virtual ~IBcoControl() = default;

    virtual core::OperationResult applyBandConfig(const core::BandConfig& config) = 0;
    virtual core::OperationResult applyBandConfigs(const std::vector<core::BandConfig>& configs) = 0;
};

class StubBcoControl final : public IBcoControl
{
public:
    core::OperationResult applyBandConfig(const core::BandConfig& config) override
    {
        return applyBandConfigs(std::vector<core::BandConfig>{config});
    }

    core::OperationResult applyBandConfigs(const std::vector<core::BandConfig>&) override
    {
        return core::OperationResult::ok();
    }
};

} // namespace siriusscope::hardware
