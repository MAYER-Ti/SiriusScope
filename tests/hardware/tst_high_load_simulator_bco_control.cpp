#include "hardware/simulator/high_load_simulator_bco_control.h"

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

class CapturingDiagnosticsSink final : public infrastructure::IDiagnosticsSink
{
public:
    void publish(const infrastructure::DiagnosticEvent& event) override
    {
        events.push_back(event);
    }

    std::vector<infrastructure::DiagnosticEvent> events;
};

class FakeBcoStreamSource final : public hardware::IBcoStreamSource
{
public:
    core::OperationResult configure(const hardware::BcoStreamConfig& config) override
    {
        ++configureCount;
        lastConfig = config;
        return configureResult;
    }

    core::OperationResult start(SampleBlockCallback) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        return core::OperationResult::ok();
    }

    hardware::BcoSourceMetrics metrics() const override
    {
        return {};
    }

    core::OperationResult configureResult = core::OperationResult::ok();
    hardware::BcoStreamConfig lastConfig;
    int configureCount = 0;
};

core::BandConfig makeBandConfig(int bandIndex, std::int64_t centerHz)
{
    const auto created = core::BandConfig::create(bandIndex, centerHz, 100'000'000LL);
    return *created.value();
}

std::vector<core::BandConfig> makeBandConfigs()
{
    return {
        makeBandConfig(0, 3'000'000'000LL),
        makeBandConfig(1, 3'200'000'000LL),
    };
}

hardware::HardwareProfile makeProfile()
{
    hardware::HardwareProfile profile;
    profile.dataSourceMode = hardware::DataSourceMode::Simulator;
    profile.bcoStreamConfig.bandConfigs = makeBandConfigs();
    profile.bcoStreamConfig.sessionId = 3;
    return profile;
}

hardware::BcoProcessingStartCommand makeStartCommand()
{
    hardware::BcoProcessingStartCommand command;
    command.bandConfigs = makeBandConfigs();
    command.timeBase = core::TimeBase{1'000, 55, core::DomainConstraints::defaultSamplePeriodNs};
    command.sessionId = 42;
    return command;
}

bool hasInfoMessageContaining(const CapturingDiagnosticsSink& diagnostics,
                              const std::string& text)
{
    return std::any_of(diagnostics.events.begin(),
                       diagnostics.events.end(),
                       [&text](const auto& event) {
                           return event.severity == infrastructure::DiagnosticSeverity::Info
                               && event.subsystem == "HighLoadSimulatorBcoControl"
                               && event.message.find(text) != std::string::npos;
                       });
}

void testApplyBandConfig(TestRunner& test)
{
    auto profile = makeProfile();
    FakeBcoStreamSource source;
    CapturingDiagnosticsSink diagnostics;
    hardware::HighLoadSimulatorBcoControl control(&profile, &source, &diagnostics);

    const auto result = control.applyBandConfig(makeBandConfig(1, 3'400'000'000LL));

    test.require(result.success, "valid BandConfig is accepted");
    test.require(profile.bcoStreamConfig.bandConfigs[1].centerFrequencyHz == 3'400'000'000LL,
                 "profile stores applied BandConfig");
    test.require(hasInfoMessageContaining(diagnostics, "applied BandConfig"),
                 "applyBandConfig publishes info diagnostic");
}

void testApplyBandConfigs(TestRunner& test)
{
    auto profile = makeProfile();
    FakeBcoStreamSource source;
    hardware::HighLoadSimulatorBcoControl control(&profile, &source);

    const std::vector<core::BandConfig> configs{
        makeBandConfig(0, 4'000'000'000LL),
        makeBandConfig(1, 4'200'000'000LL),
    };

    const auto result = control.applyBandConfigs(configs);

    test.require(result.success, "valid BandConfig set is accepted");
    test.require(profile.bcoStreamConfig.bandConfigs.size() == configs.size(),
                 "profile stores all applied BandConfigs");
    test.require(profile.bcoStreamConfig.bandConfigs[1].centerFrequencyHz == 4'200'000'000LL,
                 "profile stores final config from applied set");
}

void testRejectsInvalidBandConfig(TestRunner& test)
{
    auto profile = makeProfile();
    FakeBcoStreamSource source;
    hardware::HighLoadSimulatorBcoControl control(&profile, &source);

    const core::BandConfig invalid{0, 299'000'000LL, 100'000'000LL, true};
    const auto result = control.applyBandConfig(invalid);

    test.require(!result, "invalid BandConfig is rejected");
}

void testRejectsDuplicateBandIndex(TestRunner& test)
{
    auto profile = makeProfile();
    FakeBcoStreamSource source;
    hardware::HighLoadSimulatorBcoControl control(&profile, &source);

    const std::vector<core::BandConfig> configs{
        makeBandConfig(0, 4'000'000'000LL),
        makeBandConfig(0, 4'200'000'000LL),
    };

    const auto result = control.applyBandConfigs(configs);

    test.require(!result, "duplicate bandIndex is rejected");
}

void testStartProcessingConfiguresStreamSource(TestRunner& test)
{
    auto profile = makeProfile();
    FakeBcoStreamSource source;
    CapturingDiagnosticsSink diagnostics;
    hardware::HighLoadSimulatorBcoControl control(&profile, &source, &diagnostics);

    const auto command = makeStartCommand();
    const auto result = control.startProcessing(command);

    test.require(result.success, "startProcessing accepts valid command");
    test.require(control.processingState() == hardware::BcoProcessingState::Active,
                 "startProcessing activates control state");
    test.require(control.lastStartCommand().has_value(),
                 "startProcessing stores last command");
    test.require(source.configureCount == 1,
                 "startProcessing configures stream source");
    test.require(source.lastConfig.sessionId == command.sessionId,
                 "stream config receives command session id");
    test.require(source.lastConfig.timeBase.firstSampleIndex == command.timeBase.firstSampleIndex,
                 "stream config receives command time base");
    test.require(profile.bcoStreamConfig.sessionId == command.sessionId,
                 "profile receives command session id");
    test.require(hasInfoMessageContaining(diagnostics, "processing started"),
                 "startProcessing publishes info diagnostic");
}

void testStartProcessingRejectsInvalidTimeBase(TestRunner& test)
{
    auto profile = makeProfile();
    FakeBcoStreamSource source;
    hardware::HighLoadSimulatorBcoControl control(&profile, &source);

    auto command = makeStartCommand();
    command.timeBase.samplePeriodNs = 0;
    const auto result = control.startProcessing(command);

    test.require(!result, "startProcessing rejects invalid TimeBase");
    test.require(control.processingState() == hardware::BcoProcessingState::Failed,
                 "invalid start moves control to failed state");
    test.require(source.configureCount == 0,
                 "invalid start does not configure stream source");
}

void testStopProcessingIsIdempotent(TestRunner& test)
{
    auto profile = makeProfile();
    FakeBcoStreamSource source;
    CapturingDiagnosticsSink diagnostics;
    hardware::HighLoadSimulatorBcoControl control(&profile, &source, &diagnostics);

    const auto startResult = control.startProcessing(makeStartCommand());
    const auto firstStop = control.stopProcessing();
    const auto secondStop = control.stopProcessing();

    test.require(startResult.success, "control starts before stop test");
    test.require(firstStop.success, "first stop succeeds");
    test.require(secondStop.success, "second stop succeeds");
    test.require(control.processingState() == hardware::BcoProcessingState::Idle,
                 "stopProcessing returns control to idle");
    test.require(hasInfoMessageContaining(diagnostics, "processing stopped"),
                 "stopProcessing publishes info diagnostic");
}

} // namespace

int main()
{
    TestRunner test;

    testApplyBandConfig(test);
    testApplyBandConfigs(test);
    testRejectsInvalidBandConfig(test);
    testRejectsDuplicateBandIndex(test);
    testStartProcessingConfiguresStreamSource(test);
    testStartProcessingRejectsInvalidTimeBase(test);
    testStopProcessingIsIdempotent(test);

    return test.result();
}
