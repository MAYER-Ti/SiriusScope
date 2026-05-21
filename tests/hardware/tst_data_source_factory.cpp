#include "hardware/data_source_factory.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

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

class FakeBcoSampleSource final : public hardware::IBcoSampleSource
{
public:
    core::OperationResult start(SampleBatchCallback callback) override
    {
        callback_ = std::move(callback);
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        return core::OperationResult::ok();
    }

private:
    SampleBatchCallback callback_;
};

hardware::HardwareProfile makeProfile(hardware::DataSourceMode mode)
{
    hardware::HardwareProfile profile;
    profile.dataSourceMode = mode;
    profile.bcoStreamConfig.sessionId = 7;
    return profile;
}

void testCreatesLegacySimulatorAdapter(TestRunner& test)
{
    FakeBcoSampleSource legacySource;
    const auto profile = makeProfile(hardware::DataSourceMode::Simulator);

    auto source =
        hardware::DataSourceFactory::createBcoStreamSourceFromLegacySimulator(profile,
                                                                              &legacySource);

    test.require(source != nullptr, "factory creates legacy simulator adapter");
}

void testSimulatorFactoryRejectsNullSource(TestRunner& test)
{
    const auto profile = makeProfile(hardware::DataSourceMode::Simulator);

    auto source =
        hardware::DataSourceFactory::createBcoStreamSourceFromLegacySimulator(profile, nullptr);

    test.require(source == nullptr, "simulator factory rejects null source");
}

void testSimulatorFactoryRejectsWrongMode(TestRunner& test)
{
    FakeBcoSampleSource legacySource;
    const auto profile = makeProfile(hardware::DataSourceMode::RealHardware);

    auto source =
        hardware::DataSourceFactory::createBcoStreamSourceFromLegacySimulator(profile,
                                                                              &legacySource);

    test.require(source == nullptr, "simulator factory rejects real hardware mode");
}

void testCreatesRealSourceStub(TestRunner& test)
{
    const auto profile = makeProfile(hardware::DataSourceMode::RealHardware);

    auto source = hardware::DataSourceFactory::createRealBcoStreamSourceStub(profile);

    test.require(source != nullptr, "factory creates real source stub");
    if (!source) {
        return;
    }

    const auto startResult = source->start([](hardware::IBcoStreamSource::SampleBlockPtr) {});
    test.require(!startResult, "real source stub start fails");
    test.require(startResult.message.find("real BCO stream source is not implemented")
                     != std::string::npos,
                 "real source stub reports not implemented");
}

void testRealStubFactoryRejectsSimulatorMode(TestRunner& test)
{
    const auto profile = makeProfile(hardware::DataSourceMode::Simulator);

    auto source = hardware::DataSourceFactory::createRealBcoStreamSourceStub(profile);

    test.require(source == nullptr, "real source stub factory rejects simulator mode");
}

} // namespace

int main()
{
    TestRunner test;

    testCreatesLegacySimulatorAdapter(test);
    testSimulatorFactoryRejectsNullSource(test);
    testSimulatorFactoryRejectsWrongMode(test);
    testCreatesRealSourceStub(test);
    testRealStubFactoryRejectsSimulatorMode(test);

    return test.result();
}
