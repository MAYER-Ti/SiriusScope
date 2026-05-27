#include "hardware/adapters/legacy_bco_sample_source_adapter.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
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
        started = true;
        ++startCalls;
        callback_ = std::move(callback);
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        stopped = true;
        ++stopCalls;
        return core::OperationResult::ok();
    }

    void emitBatch(hardware::BcoSampleBatch batch) const
    {
        if (callback_) {
            callback_(batch);
        }
    }

    bool started = false;
    bool stopped = false;
    int startCalls = 0;
    int stopCalls = 0;
    SampleBatchCallback callback_;
};

hardware::BcoStreamConfig makeConfig()
{
    hardware::BcoStreamConfig config;
    config.sessionId = 42;
    return config;
}

core::SignalSample makeSample(std::uint64_t sampleIndex, int amplitude)
{
    return core::SignalSample{
        sampleIndex,
        0,
        1'000,
        3'000'001'000LL,
        amplitude,
        0,
    };
}

hardware::BcoSampleBatch makeThreeSampleBatch()
{
    hardware::BcoSampleBatch batch;
    batch.samples = {
        makeSample(10, 20),
        makeSample(11, 30),
        makeSample(12, 40),
    };
    return batch;
}

void testStartFailsWithoutSource(TestRunner& test)
{
    hardware::LegacyBcoSampleSourceAdapter adapter(nullptr);
    const auto configureResult = adapter.configure(makeConfig());
    bool callbackCalled = false;

    const auto startResult = adapter.start([&callbackCalled](auto) {
        callbackCalled = true;
    });
    const auto stopResult = adapter.stop();

    test.require(configureResult.success, "adapter accepts config without a legacy source");
    test.require(!startResult, "start fails without legacy source");
    test.require(startResult.message == "legacy BCO sample source is not configured",
                 "start without source reports configured-source error");
    test.require(!callbackCalled, "start failure without source does not call callback");
    test.require(stopResult.success, "stop without source is safe");
}

void testStartFailsWithoutConfigure(TestRunner& test)
{
    FakeBcoSampleSource source;
    hardware::LegacyBcoSampleSourceAdapter adapter(&source);

    const auto startResult = adapter.start([](auto) {});

    test.require(!startResult, "start fails before configure");
    test.require(startResult.message == "BCO stream source is not configured",
                 "start before configure reports stream configuration error");
    test.require(!source.started, "failed start before configure does not start legacy source");
    test.require(source.startCalls == 0, "failed start before configure has no legacy start call");
}

void testStartFailsWithoutCallback(TestRunner& test)
{
    FakeBcoSampleSource source;
    hardware::LegacyBcoSampleSourceAdapter adapter(&source);
    const auto configureResult = adapter.configure(makeConfig());

    const auto startResult = adapter.start({});

    test.require(configureResult.success, "adapter configures before empty callback check");
    test.require(!startResult, "start fails without stream callback");
    test.require(startResult.message == "BCO stream callback is not configured",
                 "start without callback reports callback configuration error");
    test.require(!source.started, "failed start without callback does not start legacy source");
    test.require(source.startCalls == 0, "failed start without callback has no legacy start call");
}

void testAdapterForwardsSamplesAsBlock(TestRunner& test)
{
    FakeBcoSampleSource source;
    hardware::LegacyBcoSampleSourceAdapter adapter(&source);
    const auto configureResult = adapter.configure(makeConfig());

    hardware::IBcoStreamSource::SampleBlockPtr receivedBlock;
    const auto startResult = adapter.start([&receivedBlock](auto block) {
        receivedBlock = std::move(block);
    });

    const auto batch = makeThreeSampleBatch();
    source.emitBatch(batch);

    test.require(configureResult.success, "adapter accepts stream config");
    test.require(startResult.success, "adapter starts configured legacy source");
    test.require(source.started, "adapter forwards start to legacy source");
    test.require(source.startCalls == 1, "adapter starts legacy source once");
    test.require(receivedBlock != nullptr, "adapter emits a sample block");
    if (!receivedBlock) {
        return;
    }

    test.require(receivedBlock->samples.size() == 3, "adapter copies legacy samples");
    test.require(receivedBlock->samples[0].sampleIndex == 10, "first copied sample is preserved");
    test.require(receivedBlock->samples[1].sampleIndex == 11, "second copied sample is preserved");
    test.require(receivedBlock->samples[2].sampleIndex == 12, "third copied sample is preserved");
    test.require(receivedBlock->stats.sampleCount == 3, "stats contain sample count");
    test.require(receivedBlock->stats.firstSampleIndex == 10,
                 "stats contain first sample index");
    test.require(receivedBlock->stats.lastSampleIndex == 12,
                 "stats contain last sample index");
    test.require(receivedBlock->stats.packetCount == 1,
                 "stats count one packet for non-empty legacy batch");
    test.require(receivedBlock->stats.producedAt != std::chrono::steady_clock::time_point{},
                 "stats contain production timestamp");
}

void testMetricsAreUpdated(TestRunner& test)
{
    FakeBcoSampleSource source;
    hardware::LegacyBcoSampleSourceAdapter adapter(&source);
    const auto configureResult = adapter.configure(makeConfig());
    const auto startResult = adapter.start([](auto) {});

    source.emitBatch(makeThreeSampleBatch());

    const auto metrics = adapter.metrics();

    test.require(configureResult.success, "adapter configures before metrics test");
    test.require(startResult.success, "adapter starts before metrics test");
    test.require(metrics.producedSamples == 3, "metrics count produced samples");
    test.require(metrics.producedBatches == 1, "metrics count produced batches");
    test.require(metrics.producedSamplesPerSecond >= 0.0,
                 "metrics expose non-negative sample rate");
    test.require(metrics.equivalentMegabytesPerSecond >= 0.0,
                 "metrics expose non-negative equivalent throughput");
}

void testConfigureResetsMetrics(TestRunner& test)
{
    FakeBcoSampleSource source;
    hardware::LegacyBcoSampleSourceAdapter adapter(&source);
    adapter.configure(makeConfig());
    adapter.start([](auto) {});
    source.emitBatch(makeThreeSampleBatch());

    const auto beforeReset = adapter.metrics();
    const auto configureResult = adapter.configure(makeConfig());
    const auto afterReset = adapter.metrics();

    test.require(beforeReset.producedSamples == 3,
                 "metrics have samples before configure reset");
    test.require(configureResult.success, "adapter accepts repeated configure");
    test.require(afterReset.producedSamples == 0, "configure resets produced samples");
    test.require(afterReset.producedBatches == 0, "configure resets produced batches");
}

void testStopForwardsToLegacySource(TestRunner& test)
{
    FakeBcoSampleSource source;
    hardware::LegacyBcoSampleSourceAdapter adapter(&source);

    const auto stopResult = adapter.stop();

    test.require(stopResult.success, "stop succeeds with legacy source");
    test.require(source.stopped, "adapter forwards stop to legacy source");
    test.require(source.stopCalls == 1, "adapter stops legacy source once");
}

} // namespace

int main()
{
    TestRunner test;

    testStartFailsWithoutSource(test);
    testStartFailsWithoutConfigure(test);
    testStartFailsWithoutCallback(test);
    testAdapterForwardsSamplesAsBlock(test);
    testMetricsAreUpdated(test);
    testConfigureResetsMetrics(test);
    testStopForwardsToLegacySource(test);

    return test.result();
}
