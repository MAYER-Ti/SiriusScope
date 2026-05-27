#pragma once

namespace siriusscope::hardware {

struct SimulatorPulseBandConfig
{
    int bandIndex = 0;
    bool enabled = true;
    double pulsePeriodUs = 100000.0;
    double pulseWidthUs = 10000.0;
};

} // namespace siriusscope::hardware
