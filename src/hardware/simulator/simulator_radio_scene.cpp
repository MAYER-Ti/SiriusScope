#include "hardware/simulator/simulator_radio_scene.h"

namespace siriusscope::hardware {

SimulatorRadioScene makeDefaultSimulatorRadioScene()
{
    return SimulatorRadioScene{{
        {45.0, 2'920'000'000LL, 112, 22.0, false, 0, 0},
        {95.0, 5'825'000'000LL, 105, 22.0, false, 0, 0},
        {135.0, 8'250'000'000LL, 116, 22.0, false, 0, 0},
        {250.0, 9'670'000'000LL, 96, 24.0, true, 8'000'000LL, 80},
        {310.0, 14'190'000'000LL, 82, 26.0, true, 12'000'000LL, 100},
    }};
}

} // namespace siriusscope::hardware
