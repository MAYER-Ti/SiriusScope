#include "hardware/simulator/simulator_radio_scene.h"

namespace siriusscope::hardware {

SimulatorRadioScene makeDefaultSimulatorRadioScene()
{
    return SimulatorRadioScene{{
        {0, 45.0, -80'000'000LL, 112, 22.0, false, 0, 0},
        {1, 95.0, 30'000'000LL, 105, 22.0, false, 0, 0},
        {2, 135.0, 0LL, 116, 22.0, false, 0, 0},
        {3, 250.0, 120'000'000LL, 96, 24.0, true, 8'000'000LL, 80},
        {4, 310.0, -60'000'000LL, 82, 26.0, true, 12'000'000LL, 100},
    }};
}

} // namespace siriusscope::hardware
