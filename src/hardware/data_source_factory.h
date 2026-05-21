#pragma once

#include "hardware/hardware_profile.h"
#include "hardware/interfaces/bco_sample_source.h"
#include "hardware/interfaces/bco_stream_source.h"

#include <memory>

namespace siriusscope::hardware {

class DataSourceFactory
{
public:
    static std::unique_ptr<IBcoStreamSource> createBcoStreamSourceFromLegacySimulator(
        const HardwareProfile& profile,
        IBcoSampleSource* legacySimulatorSource);

    static std::unique_ptr<IBcoStreamSource> createRealBcoStreamSourceStub(
        const HardwareProfile& profile);
};

} // namespace siriusscope::hardware
