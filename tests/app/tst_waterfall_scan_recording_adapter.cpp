#include "app/frequencyviewportmodel.h"
#include "app/waterfallcontroller.h"
#include "app/waterfallscanrecordingadapter.h"

#include <QCoreApplication>

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
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

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

class FakeSampleSource final : public hardware::IBcoSampleSource
{
public:
    core::OperationResult start(SampleBatchCallback callback) override
    {
        m_callback = std::move(callback);
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        m_callback = {};
        return core::OperationResult::ok();
    }

private:
    SampleBatchCallback m_callback;
};

std::vector<core::BandConfig> makeBandConfigs()
{
    std::vector<core::BandConfig> bands;
    for (int bandIndex = 0; bandIndex < core::DomainConstraints::currentBandCount; ++bandIndex) {
        const auto created = core::BandConfig::create(
            bandIndex,
            550'000'000LL + static_cast<std::int64_t>(bandIndex)
                * core::DomainConstraints::maxBandWidthHz,
            core::DomainConstraints::maxBandWidthHz);
        bands.push_back(*created.value());
    }
    return bands;
}

struct Fixture
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    infrastructure::NullDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    app::WaterfallController controller{&viewport,
                                        &source,
                                        makeBandConfigs(),
                                        &storage,
                                        &diagnostics,
                                        app::WaterfallControllerConfig{}};
    app::WaterfallScanRecordingAdapter adapter{&controller};
};

void testBeginStartsRecording(TestRunner& test)
{
    Fixture fixture;

    const auto result = fixture.adapter.beginScanRecording(1);

    test.require(result.success, "beginScanRecording returns ok");
    test.require(fixture.controller.sessionActive(),
                 "beginScanRecording starts Waterfall recording");
}

void testEndStopsRecording(TestRunner& test)
{
    Fixture fixture;

    fixture.adapter.beginScanRecording(1);
    const auto result = fixture.adapter.endScanRecording(1);

    test.require(result.success, "endScanRecording returns ok");
    test.require(!fixture.controller.sessionActive(),
                 "endScanRecording stops Waterfall recording");
}

void testRepeatedBeginAndEndWithoutActiveAreSafe(TestRunner& test)
{
    Fixture fixture;

    const auto endWithoutActive = fixture.adapter.endScanRecording(1);
    const auto firstBegin = fixture.adapter.beginScanRecording(1);
    const auto secondBegin = fixture.adapter.beginScanRecording(1);
    const bool activeAfterRepeatedBegin = fixture.controller.sessionActive();
    const auto end = fixture.adapter.endScanRecording(1);
    const auto secondEnd = fixture.adapter.endScanRecording(1);

    test.require(endWithoutActive.success, "end without active recording is ok");
    test.require(firstBegin.success, "first begin succeeds");
    test.require(secondBegin.success, "repeated begin succeeds");
    test.require(activeAfterRepeatedBegin, "repeated begin keeps recording active");
    test.require(end.success, "end after repeated begin succeeds");
    test.require(secondEnd.success, "repeated end succeeds");
    test.require(!fixture.controller.sessionActive(), "recording is stopped after end");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testBeginStartsRecording(test);
    testEndStopsRecording(test);
    testRepeatedBeginAndEndWithoutActiveAreSafe(test);

    return test.result();
}
