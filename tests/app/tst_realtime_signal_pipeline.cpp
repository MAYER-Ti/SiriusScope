#include "app/bearingframebus.h"
#include "app/realtimesignalpipeline.h"
#include "app/signalsamplebus.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
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

core::BandConfig makeBandConfig(int bandIndex)
{
    const auto centerHz = 550'000'000LL
        + static_cast<std::int64_t>(bandIndex) * core::DomainConstraints::maxBandWidthHz;
    const auto created =
        core::BandConfig::create(bandIndex, centerHz, core::DomainConstraints::maxBandWidthHz);
    return *created.value();
}

processing::SampleProcessingConfig makeConfig(std::vector<core::BandConfig> bands)
{
    processing::SampleProcessingConfig config;
    config.bands = std::move(bands);
    config.aggregationWindow.frequencyBinWidthHz = 100'000'000LL;
    return config;
}

core::SignalSample makeSample(const core::BandConfig& band,
                              std::uint64_t sampleIndex,
                              int beamIndex,
                              int amplitude)
{
    const auto created = core::SignalSample::create(
        core::BeamSample{sampleIndex, 0, amplitude, beamIndex},
        band);
    return *created.value();
}

processing::SampleBatch makeTwoBeamBatch(const core::BandConfig& band,
                                         std::uint64_t sampleIndex = 1)
{
    processing::SampleBatch batch;
    batch.samples = {
        makeSample(band, sampleIndex, 0, 90),
        makeSample(band, sampleIndex, 1, 40),
    };
    return batch;
}

bool hasErrorDiagnostic(const processing::SampleProcessingResult& result)
{
    return std::any_of(result.diagnostics.cbegin(),
                       result.diagnostics.cend(),
                       [](const auto& diagnostic) {
                           return diagnostic.severity
                               == processing::ProcessingDiagnosticSeverity::Error;
                       });
}

void testEmptyBatchProducesEmptyProcessingResult(TestRunner& test)
{
    const auto config = makeConfig({makeBandConfig(0)});
    app::RealtimeSignalPipeline pipeline(app::RealtimeSignalPipelineConfig{config});

    const auto result = pipeline.process(app::RealtimeSignalPipelineInput{});

    test.require(result.inputSampleCount == 0, "empty batch reports zero input samples");
    test.require(result.emptyBatchCount == 1, "empty batch increments emptyBatchCount");
    test.require(result.processingResult.acceptedSamples.empty(),
                 "empty batch has no accepted samples");
    test.require(result.processingResult.waterfallFrame.rows.empty(),
                 "empty batch has no waterfall rows");
    test.require(result.processingResult.bearingFrames.empty(),
                 "empty batch has no bearing frames");
    test.require(!result.renderResult, "empty batch has no render result");
    test.require(result.processingResult.diagnostics.empty(),
                 "pipeline keeps empty batch processing result empty");
}

void testValidSamplesAreProcessed(TestRunner& test)
{
    const auto band = makeBandConfig(0);
    const auto config = makeConfig({band});
    app::RealtimeSignalPipeline pipeline(app::RealtimeSignalPipelineConfig{config});

    auto batch = makeTwoBeamBatch(band);
    const auto result = pipeline.process(app::RealtimeSignalPipelineInput{std::move(batch)});

    test.require(result.inputSampleCount == 2, "valid batch reports input sample count");
    test.require(result.emptyBatchCount == 0, "valid batch is not counted as empty");
    test.require(!result.processingResult.acceptedSamples.empty(),
                 "valid batch produces accepted samples");
    test.require(!result.processingResult.waterfallFrame.rows.empty()
                     || !result.processingResult.bearingFrames.empty(),
                 "valid batch produces processing output frames");
    test.require(!hasErrorDiagnostic(result.processingResult),
                 "valid batch has no critical processing diagnostics");
}

void testValidBatchCreatesRenderResult(TestRunner& test)
{
    const auto band = makeBandConfig(0);
    const auto config = makeConfig({band});
    app::RealtimeSignalPipeline pipeline(app::RealtimeSignalPipelineConfig{
        config,
        nullptr,
        nullptr,
        300e6,
        18e9,
        64,
    });

    auto batch = makeTwoBeamBatch(band);
    const auto result = pipeline.process(app::RealtimeSignalPipelineInput{
        std::move(batch),
        123456789,
    });

    test.require(result.renderResult.has_value(), "valid batch creates render result");
    test.require(result.renderResult && result.renderResult->row.utcMs == 123456789,
                 "render result preserves input UTC time");
    test.require(result.renderResult && result.renderResult->row.bins.size() == 64,
                 "render result uses configured bin count");
}

void testRenderContextChangesBinCount(TestRunner& test)
{
    const auto band = makeBandConfig(0);
    const auto config = makeConfig({band});
    app::RealtimeSignalPipeline pipeline(app::RealtimeSignalPipelineConfig{
        config,
        nullptr,
        nullptr,
        300e6,
        18e9,
        16,
    });

    auto firstBatch = makeTwoBeamBatch(band, 1);
    const auto firstResult = pipeline.process(app::RealtimeSignalPipelineInput{
        std::move(firstBatch),
        1,
    });

    pipeline.setWaterfallRenderContext(300e6, 18e9, 32);

    auto secondBatch = makeTwoBeamBatch(band, 2);
    const auto secondResult = pipeline.process(app::RealtimeSignalPipelineInput{
        std::move(secondBatch),
        2,
    });

    test.require(firstResult.renderResult && firstResult.renderResult->row.bins.size() == 16,
                 "initial render context controls bin count");
    test.require(secondResult.renderResult && secondResult.renderResult->row.bins.size() == 32,
                 "updated render context controls bin count");
}

void testInvalidRenderBinCountIsSafe(TestRunner& test)
{
    const auto band = makeBandConfig(0);
    const auto config = makeConfig({band});
    app::RealtimeSignalPipeline pipeline(app::RealtimeSignalPipelineConfig{
        config,
        nullptr,
        nullptr,
        300e6,
        18e9,
        0,
    });

    auto firstBatch = makeTwoBeamBatch(band, 1);
    const auto firstResult = pipeline.process(app::RealtimeSignalPipelineInput{
        std::move(firstBatch),
        1,
    });

    pipeline.setWaterfallRenderContext(300e6, 18e9, 0);

    auto secondBatch = makeTwoBeamBatch(band, 2);
    const auto secondResult = pipeline.process(app::RealtimeSignalPipelineInput{
        std::move(secondBatch),
        2,
    });

    test.require(firstResult.renderResult && firstResult.renderResult->row.bins.size() >= 1,
                 "invalid constructor bin count is normalized");
    test.require(secondResult.renderResult && secondResult.renderResult->row.bins.size() >= 1,
                 "invalid setter bin count is normalized");
}

void testPipelinePublishesRawSamples(TestRunner& test)
{
    const auto band = makeBandConfig(0);
    const auto config = makeConfig({band});
    app::SignalSampleBus signalSampleBus;
    int receivedSampleCount = 0;
    signalSampleBus.subscribe([&receivedSampleCount](std::vector<core::SignalSample> samples) {
        receivedSampleCount += static_cast<int>(samples.size());
    });

    app::RealtimeSignalPipeline pipeline(
        app::RealtimeSignalPipelineConfig{config, &signalSampleBus, nullptr});

    auto batch = makeTwoBeamBatch(band);
    const auto result = pipeline.process(app::RealtimeSignalPipelineInput{std::move(batch)});

    test.require(result.inputSampleCount == 2, "raw sample publication preserves input count");
    test.require(receivedSampleCount == 2, "pipeline publishes raw samples to SignalSampleBus");
}

void testEmptyBatchDoesNotPublishRawSamples(TestRunner& test)
{
    const auto config = makeConfig({makeBandConfig(0)});
    app::SignalSampleBus signalSampleBus;
    int receivedSampleCount = 0;
    signalSampleBus.subscribe([&receivedSampleCount](std::vector<core::SignalSample> samples) {
        receivedSampleCount += static_cast<int>(samples.size());
    });

    app::RealtimeSignalPipeline pipeline(
        app::RealtimeSignalPipelineConfig{config, &signalSampleBus, nullptr});

    const auto result = pipeline.process(app::RealtimeSignalPipelineInput{});

    test.require(result.emptyBatchCount == 1, "empty batch is reported by pipeline");
    test.require(receivedSampleCount == 0, "empty batch does not publish raw samples");
}

void testPipelinePublishesBearingFrames(TestRunner& test)
{
    const auto band = makeBandConfig(0);
    const auto config = makeConfig({band});
    app::BearingFrameBus bearingFrameBus;
    int receivedBearingFrameCount = 0;
    bearingFrameBus.subscribe(
        [&receivedBearingFrameCount](std::vector<processing::BearingInputFrame> frames) {
            receivedBearingFrameCount += static_cast<int>(frames.size());
        });

    app::RealtimeSignalPipeline pipeline(
        app::RealtimeSignalPipelineConfig{config, nullptr, &bearingFrameBus});

    auto batch = makeTwoBeamBatch(band);
    const auto result = pipeline.process(app::RealtimeSignalPipelineInput{std::move(batch)});

    test.require(!result.processingResult.bearingFrames.empty(),
                 "two-beam batch produces bearing frames");
    test.require(receivedBearingFrameCount > 0,
                 "pipeline publishes bearing frames to BearingFrameBus");
}

void testDisablingBusesIsSafe(TestRunner& test)
{
    const auto band = makeBandConfig(0);
    const auto config = makeConfig({band});
    app::RealtimeSignalPipeline pipeline(
        app::RealtimeSignalPipelineConfig{config, nullptr, nullptr});

    auto batch = makeTwoBeamBatch(band);
    const auto result = pipeline.process(app::RealtimeSignalPipelineInput{std::move(batch)});

    test.require(result.inputSampleCount == 2, "pipeline reports input count with disabled buses");
    test.require(!result.processingResult.waterfallFrame.rows.empty()
                     || !result.processingResult.bearingFrames.empty(),
                 "pipeline still returns processing output with disabled buses");
}

void testSetProcessingConfigChangesAcceptedBands(TestRunner& test)
{
    const auto firstBand = makeBandConfig(0);
    const auto secondBand = makeBandConfig(1);
    app::RealtimeSignalPipeline pipeline(
        app::RealtimeSignalPipelineConfig{makeConfig({firstBand})});

    processing::SampleBatch firstBatch;
    firstBatch.samples = {
        makeSample(firstBand, 1, 0, 80),
        makeSample(firstBand, 1, 1, 50),
    };
    const auto firstResult =
        pipeline.process(app::RealtimeSignalPipelineInput{std::move(firstBatch)});

    pipeline.setProcessingConfig(makeConfig({secondBand}));

    processing::SampleBatch secondBatch;
    secondBatch.samples = {
        makeSample(secondBand, 1, 0, 70),
        makeSample(secondBand, 1, 1, 45),
    };
    const auto secondResult =
        pipeline.process(app::RealtimeSignalPipelineInput{std::move(secondBatch)});

    test.require(!firstResult.processingResult.acceptedSamples.empty(),
                 "initial config accepts first band samples");
    test.require(!secondResult.processingResult.acceptedSamples.empty(),
                 "updated config accepts new band samples");
    test.require(!secondResult.processingResult.bearingFrames.empty()
                     || !secondResult.processingResult.waterfallFrame.rows.empty(),
                 "updated config produces processing output");
    test.require(!hasErrorDiagnostic(secondResult.processingResult),
                 "updated config has no critical diagnostics");
}

} // namespace

int main()
{
    TestRunner test;

    testEmptyBatchProducesEmptyProcessingResult(test);
    testValidSamplesAreProcessed(test);
    testValidBatchCreatesRenderResult(test);
    testRenderContextChangesBinCount(test);
    testInvalidRenderBinCountIsSafe(test);
    testPipelinePublishesRawSamples(test);
    testEmptyBatchDoesNotPublishRawSamples(test);
    testPipelinePublishesBearingFrames(test);
    testDisablingBusesIsSafe(test);
    testSetProcessingConfigChangesAcceptedBands(test);

    return test.result();
}
