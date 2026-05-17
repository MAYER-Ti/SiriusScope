#pragma once

#include "app/scanacquisitionsession.h"
#include "core/operation_result.h"
#include "processing/bearing_service.h"

#include <cstdint>
#include <vector>

namespace siriusscope::app {

class IScanAcquisitionRecorder
{
public:
    virtual ~IScanAcquisitionRecorder() = default;

    virtual core::OperationResult begin(const ScanAcquisitionMetadata& metadata) = 0;
    virtual core::OperationResult append(
        const processing::BearingFrameObservation& observation) = 0;
    virtual core::OperationResult close(const ScanAcquisitionMetadata& finalMetadata) = 0;
    virtual std::vector<processing::BearingFrameObservation> observations(
        std::uint64_t scanSessionId) const = 0;
    virtual bool active() const noexcept = 0;
};

} // namespace siriusscope::app
