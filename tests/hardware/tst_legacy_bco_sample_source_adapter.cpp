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
        ++startCalls;
        callback_ = std::move(callback);
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        ++stopCalls;
        return core::OperationResult::ok();
    }

    void emitBatch(const hardware::BcoSampleBatch& batch) const
    {
        if (callback_) {
            callback_(batch);
        }
    }

    int startCalls = 0;
    int stopCalls = 0;

private:
    SampleBatchCallback callback_;
};

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

void testAdapterWrapsLegacyBatch(TestRunner& test)
{
    FakeBcoSampleSource legacySource;
    hardware::LegacyBcoSampleSourceAdapter adapter(&legacySource);

    hardware::BcoStreamConfig config;
    config.sessionId = 42;
    const auto configureResult = adapter.configure(config);

    hardware::IBcoStreamSource::SampleBlockPtr receivedBlock;
    const auto startResult = adapter.start([&receivedBlock](auto block) {
        receivedBlock = std::move(block);
    });

    hardware::BcoSampleBatch batch;
    batch.samples = {
        makeSample(10, 20),
        makeSample(11, 30),
        makeSample(12, 40),
    };
    legacySource.emitBatch(batch);

    const auto metrics = adapter.metrics();
    const auto stopResult = adapter.stop();

    test.require(configureResult.success, "adapter accepts stream config");
    test.require(startResult.success, "adapter starts the legacy source");
    test.require(legacySource.startCalls == 1, "adapter proxies start");
    test.require(receivedBlock != nullptr, "adapter emits a sample block");
    if (receivedBlock) {
        test.require(receivedBlock->samples.size() == batch.samples.size(),
                     "adapter copies legacy samples");
        test.require(receivedBlock->stats.sampleCount == 3, "stats contain sample count");
        test.require(receivedBlock->stats.firstSampleIndex == 10,
                     "stats contain first sample index");
        test.require(receivedBlock->stats.lastSampleIndex == 12,
                     "stats contain last sample index");
        test.require(receivedBlock->stats.producedAt
                         != std::chrono::steady_clock::time_point{},
                     "stats contain production timestamp");
    }
    test.require(metrics.producedSamples == 3, "metrics count produced samples");
    test.require(metrics.producedBatches == 1, "metrics count produced batches");
    test.require(stopResult.success, "adapter stops the legacy source");
    test.require(legacySource.stopCalls == 1, "adapter proxies stop");
}

} // namespace

int main()
{
    TestRunner test;

    testAdapterWrapsLegacyBatch(test);

    return test.result();
}
