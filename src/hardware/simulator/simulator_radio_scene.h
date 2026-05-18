#pragma once

#include <cstdint>
#include <vector>

namespace siriusscope::hardware {

struct SimulatedRadioSource
{
    double azimuthDeg = 45.0;
    // Physical source frequency; BandConfig only decides whether it is received.
    std::int64_t absoluteFrequencyHz = 0;
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
