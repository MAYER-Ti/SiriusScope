#pragma once

#include "processing/sample_processor.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace siriusscope::processing {

struct BearingFrameObservation
{
    BearingInputFrame frame;
    double antennaAzimuthDeg = 0.0;
    std::int64_t observedUtcNs = 0;
};

struct BearingServiceConfig
{
    int leftBeamIndex = 0;
    int rightBeamIndex = 1;
    double beamHalfWidthDeg = 30.0;
    int minCandidateAmplitude = 1;
    double minResultQuality = 0.05;
    std::size_t topObservationCount = 5;
};

struct BearingCalculationResult
{
    std::vector<core::BearingResult> results;
    std::vector<ProcessingDiagnostic> diagnostics;
};

class BearingService
{
public:
    explicit BearingService(BearingServiceConfig config = {});
    virtual ~BearingService() = default;

    virtual BearingCalculationResult calculate(
        const std::vector<BearingFrameObservation>& observations,
        const core::TimeBase& timeBase,
        const core::RuntimeCapabilities& capabilities =
            core::defaultRuntimeCapabilities()) const;

private:
    BearingServiceConfig m_config;
};

} // namespace siriusscope::processing
