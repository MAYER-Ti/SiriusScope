#include "bco_generator/generator.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
int failures = 0;
void require(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
}

void testPulseAndTime()
{
    using namespace bco_generator;
    const std::array bands{Band{0, 3'000'000'000LL, 2'750'000'000LL, 3'250'000'000LL}};
    const std::array sources{RadioSource{0, 3'000'000'000LL, 100, 30, false, 0, 0}};
    const std::array pulses{PulseConfig{0, true, 20, 3}};
    std::array<Sample, 10> output;
    BatchRequest request{bands, pulses, sources, 42, 42, 1000};
    auto result = generate(request, output);
    require(result.status == Status::Ok && result.sampleCount == 6
                && result.nextSampleIndex == 52, "pulse batch size and next time slot");
    for (std::size_t i = 0; i < 6; ++i) {
        const auto& record = output[i];
        require(record.sampleIndex == 42 + i / 2 && record.beamIndex == int(i % 2)
                    && record.bandIndex == 0 && record.amplitude == 61
                    && record.frequencyOffsetHz == 0
                    && record.absoluteFrequencyHz == 3'000'000'000LL,
                "pulse reference beam pair");
    }
    request.firstSampleIndex = result.nextSampleIndex;
    result = generate(request, output);
    require(result.sampleCount == 0 && result.nextSampleIndex == 62,
            "empty pulse interval advances time");
    request.firstSampleIndex = result.nextSampleIndex;
    result = generate(request, output);
    require(result.sampleCount == 6 && output[0].sampleIndex == 62,
            "pulse restarts at fixed time origin");
    request.pulses = {};
    request.firstSampleIndex = 42;
    output[5].amplitude = 99;
    result = generate(request, std::span(output).first(5));
    require(result.sampleCount == 5 && result.nextSampleIndex == 47
                && output[4].sampleIndex == 44 && output[4].beamIndex == 0,
            "odd budget compatibility");
    require(output[5].amplitude == 99, "output span boundary is respected");
    result = generate(request, {});
    require(result.sampleCount == 0 && result.nextSampleIndex == 42, "zero budget");
    request.sources = {};
    result = generate(request, output);
    require(result.sampleCount == 0 && result.nextSampleIndex == 52, "empty scene");
}

void testScratchAndFastPath()
{
    using namespace bco_generator;
    const std::array bands{Band{0, 3'000'000'000LL, 2'750'000'000LL, 3'250'000'000LL}};
    const std::array sources{RadioSource{0, 3'000'000'000LL, 100, 30, true, 10'000'000, 4}};
    std::array<Sample, 10> output{};
    std::array<Sample, 2> scratch{};
    BatchRequest request{bands, {}, sources, 42, 42, 1000, 0, 0, true};
    output[0].amplitude = 99;
    auto result = generate(request, output, std::span(scratch).first(1));
    require(result.status == Status::InsufficientScratch && result.sampleCount == 0
                && result.nextSampleIndex == 42 && output[0].amplitude == 99,
            "small scratch rejected without output/time changes");
    result = generate(request, output, scratch);
    require(result.status == Status::Ok && result.sampleCount == 10
                && result.nextSampleIndex == 52, "fast batch");
    for (std::size_t i = 0; i < output.size(); ++i) {
        require(output[i].sampleIndex == 42 + i && output[i].beamIndex == int(i % 2)
                    && output[i].absoluteFrequencyHz == 3'000'000'000LL
                    && output[i].amplitude == 61,
                "fast compatibility: fixed frequency and per-record index");
    }
    request.fastContinuous = false;
    result = generate(request, output);
    require(result.sampleCount == 10 && output[2].absoluteFrequencyHz == 3'010'000'000LL
                && output[6].absoluteFrequencyHz == 2'990'000'000LL,
            "physical path applies frequency drift");
    // Repeating a request needs no reset and no wall-clock or global state.
    const auto previous = output;
    generate(request, output);
    for (std::size_t i = 0; i < output.size(); ++i) {
        require(output[i].absoluteFrequencyHz == previous[i].absoluteFrequencyHz
                    && output[i].sampleIndex == previous[i].sampleIndex,
                "stateless deterministic replay");
    }
    request.fastContinuous = true;
    request.firstSampleIndex = std::numeric_limits<std::uint64_t>::max() - 1;
    result = generate(request, output, scratch);
    require(result.nextSampleIndex == std::numeric_limits<std::uint64_t>::max()
                && output[2].sampleIndex == std::numeric_limits<std::uint64_t>::max(),
            "sample index saturates instead of wrapping");
}

void testVisibilityAndInvalidTiming()
{
    using namespace bco_generator;
    const std::array bands{Band{0, 3'000'000'000LL, 2'750'000'000LL, 3'250'000'000LL}};
    const std::array sources{RadioSource{0, 2'750'000'000LL, 100, 30, false, 0, 0},
                             RadioSource{0, 2'749'999'999LL, 100, 30, false, 0, 0}};
    std::array<Sample, 4> output{};
    BatchRequest request{bands, {}, sources, 42, 42, 1000, 0, 61};
    auto result = generate(request, output);
    require(result.sampleCount == 4 && output[0].frequencyOffsetHz == -250'000'000,
            "inclusive band edge, out-of-band source filtered");
    request.minVisibleAmplitude = 62;
    result = generate(request, output);
    require(result.sampleCount == 0, "amplitude threshold excludes both beams");
    const std::array invalidPulse{PulseConfig{0, true, 0, 3}};
    require(!hasValidPulseTiming(invalidPulse[0]), "invalid timing detected");
    request.minVisibleAmplitude = 0;
    request.pulses = invalidPulse;
    result = generate(request, output);
    require(result.sampleCount == 4, "legacy invalid pulse timing falls back to continuous");
}
} // namespace

int main()
{
    testPulseAndTime();
    testScratchAndFastPath();
    testVisibilityAndInvalidTiming();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
