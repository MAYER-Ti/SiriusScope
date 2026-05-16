#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"

namespace siriusscope::hardware {

class IAntennaControl
{
public:
    virtual ~IAntennaControl() = default;

    virtual core::OperationResult moveToAzimuth(double azimuthDeg) = 0;
    virtual core::OperationResult startSectorScan(const core::ScanSector& sector) = 0;
    virtual core::OperationResult stop() = 0;
};

class StubAntennaControl final : public IAntennaControl
{
public:
    core::OperationResult moveToAzimuth(double) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult startSectorScan(const core::ScanSector&) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        return core::OperationResult::ok();
    }
};

} // namespace siriusscope::hardware
