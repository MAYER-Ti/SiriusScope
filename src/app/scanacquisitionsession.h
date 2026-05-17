#pragma once

#include "core/domain_models.h"
#include "processing/bearing_service.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::app {

struct ScanAcquisitionMetadata
{
    std::uint64_t scanSessionId = 0;
    core::ScanSector requestedSector;
    std::chrono::system_clock::time_point startedAt;
    std::chrono::system_clock::time_point finishedAt;
    double startAzimuthDeg = 0.0;
    double endAzimuthDeg = 0.0;
    double speedDegPerSec = 0.0;
    std::optional<core::TimeBase> timeBase;
};

struct ScanAcquisitionSession
{
    ScanAcquisitionMetadata metadata;
    std::vector<processing::BearingFrameObservation> observations;
};

} // namespace siriusscope::app
