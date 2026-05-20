#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"

#include <cstdint>
#include <vector>

namespace siriusscope::hardware {

enum class BcoProcessingState
{
    Idle,
    Starting,
    Active,
    Stopping,
    Failed,
};

struct BcoProcessingStartCommand
{
    std::vector<core::BandConfig> bandConfigs;
    core::TimeBase timeBase;
    std::uint64_t sessionId = 0;
};

class IBcoControl
{
public:
    virtual ~IBcoControl() = default;

    virtual core::OperationResult applyBandConfig(const core::BandConfig& config) = 0;
    virtual core::OperationResult applyBandConfigs(const std::vector<core::BandConfig>& configs) = 0;
    virtual core::OperationResult startProcessing(const BcoProcessingStartCommand& command) = 0;
    virtual core::OperationResult stopProcessing() = 0;
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

    core::OperationResult startProcessing(const BcoProcessingStartCommand&) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult stopProcessing() override
    {
        return core::OperationResult::ok();
    }
};

} // namespace siriusscope::hardware
