#pragma once

#include <cstdint>
#include <vector>

namespace siriusscope::hardware {

struct SimulatedRadioSource
{
    int bandIndex = 0;
    double azimuthDeg = 45.0;
    std::int64_t frequencyOffsetHz = 0;
    int peakAmplitude = 110;
    double beamSigmaDeg = 22.0;
    bool frequencyDriftEnabled = false;
    std::int64_t driftSpanHz = 5'000'000LL;
    std::uint64_t driftPeriodSteps = 60;
};

struct SimulatorRadioScene
{
    std::vector<SimulatedRadioSource> sources;
};

SimulatorRadioScene makeDefaultSimulatorRadioScene();

} // namespace siriusscope::hardware
