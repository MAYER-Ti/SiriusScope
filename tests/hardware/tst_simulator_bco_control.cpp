#include "hardware/simulator/simulator_bco_control.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace siriusscope;

class TestRunner
{
public:
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            ++m_failed;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    int result() const noexcept
    {
        return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

private:
    int m_failed = 0;
};

core::BandConfig makeBandConfig(int bandIndex, std::int64_t centerHz)
{
    const auto config = core::BandConfig::create(bandIndex, centerHz, 100'000'000LL);
    return *config.value();
}

const core::BandConfig* findConfig(const std::vector<core::BandConfig>& configs, int bandIndex)
{
    const auto found = std::find_if(configs.begin(), configs.end(), [bandIndex](const auto& config) {
        return config.bandIndex == bandIndex;
    });

    return found == configs.end() ? nullptr : &(*found);
}

void testApplyBandConfig(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source;
    hardware::SimulatorBcoControl control(&source);

    const auto config = makeBandConfig(1, 6'000'000'000LL);
    const auto result = control.applyBandConfig(config);
    const auto configs = source.bandConfigs();
    const auto* applied = findConfig(configs, 1);

    test.require(result.success, "valid BandConfig is accepted");
    test.require(applied != nullptr && applied->centerFrequencyHz == 6'000'000'000LL,
                 "source uses new centerFrequencyHz after applyBandConfig");
}

void testApplyBandConfigs(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source;
    hardware::SimulatorBcoControl control(&source);

    std::vector<core::BandConfig> configs;
    for (int bandIndex = 0; bandIndex < core::DomainConstraints::currentBandCount; ++bandIndex) {
        configs.push_back(makeBandConfig(bandIndex,
                                         1'000'000'000LL
                                             + static_cast<std::int64_t>(bandIndex)
                                                 * 600'000'000LL));
    }

    const auto result = control.applyBandConfigs(configs);
    const auto stored = source.bandConfigs();

    test.require(result.success, "valid BandConfig set is accepted");
    test.require(stored.size() == configs.size(), "source stores all applied BandConfigs");
    test.require(findConfig(stored, 4) != nullptr
                     && findConfig(stored, 4)->centerFrequencyHz == 3'400'000'000LL,
                 "source stores final config from applied set");
}

void testRejectsInvalidBandConfig(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source;
    hardware::SimulatorBcoControl control(&source);

    const core::BandConfig invalid{0, 299'000'000LL, 100'000'000LL, true};
    const auto result = control.applyBandConfig(invalid);

    test.require(!result, "invalid BandConfig is rejected");
}

void testRejectsDuplicateBandIndex(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source;
    hardware::SimulatorBcoControl control(&source);

    const std::vector<core::BandConfig> configs{
        makeBandConfig(0, 1'000'000'000LL),
        makeBandConfig(0, 1'200'000'000LL),
    };

    const auto result = control.applyBandConfigs(configs);
    test.require(!result, "duplicate bandIndex is rejected");
}

void testRejectsUnknownBandIndexForSingleApply(TestRunner& test)
{
    hardware::SimulatorBcoSampleSource source;
    hardware::SimulatorBcoControl control(&source);

    source.setBandConfigs({makeBandConfig(0, 1'000'000'000LL)});

    const auto result = control.applyBandConfig(makeBandConfig(1, 1'600'000'000LL));
    test.require(!result, "applyBandConfig rejects bandIndex absent in source config set");
}

} // namespace

int main()
{
    TestRunner test;

    testApplyBandConfig(test);
    testApplyBandConfigs(test);
    testRejectsInvalidBandConfig(test);
    testRejectsDuplicateBandIndex(test);
    testRejectsUnknownBandIndexForSingleApply(test);

    return test.result();
}
