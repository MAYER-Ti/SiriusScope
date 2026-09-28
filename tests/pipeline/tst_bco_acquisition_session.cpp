#include "pipeline/bco_acquisition_session.h"
#include "pipeline/data_ingest_pipeline.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
using namespace siriusscope;
int failures = 0;
void require(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAILED: " << message << '\n'; }
}

class Source final : public hardware::IBcoStreamSource
{
public:
    SampleBlockCallback callback;
    int starts = 0;
    int stops = 0;
    bool failStart = false;
    bool emitDuringStop = false;
    core::OperationResult configure(const hardware::BcoStreamConfig&) override
    { return core::OperationResult::ok(); }
    core::OperationResult start(SampleBlockCallback value) override
    {
        ++starts;
        if (failStart) return core::OperationResult::failure("injected start failure");
        callback = std::move(value);
        return core::OperationResult::ok();
    }
    core::OperationResult stop() override
    {
        ++stops;
        if (emitDuringStop) emitBlock();
        callback = {};
        return core::OperationResult::ok();
    }
    hardware::BcoSourceMetrics metrics() const override { return {}; }
    void emitBlock()
    {
        if (!callback) return;
        auto block = std::make_shared<hardware::BcoSampleBlock>();
        block->samples.push_back({42, 0, 0, 3'000'000'000LL, 80, 0});
        block->stats.sampleCount = 1;
        block->stats.firstSampleIndex = block->stats.lastSampleIndex = 42;
        block->stats.antennaAzimuthDeg = 45;
        callback(block);
    }
};

pipeline::DataIngestPipelineConfig config()
{
    pipeline::DataIngestPipelineConfig result;
    result.blockPool = {4, 16};
    result.queueCapacity = 4;
    result.acceptingOnStart = true;
    return result;
}

void testHeadlessDeliveryAndRecordingGate()
{
    Source source;
    pipeline::DataIngestPipeline pipeline(config());
    pipeline::BcoAcquisitionSession session(&source, &pipeline);
    require(session.startSource().success, "source starts without QObject/presentation");
    require(session.startSource().success && source.starts == 1, "start is idempotent");
    source.emitBlock();
    require(session.metrics().receivedBlocks == 0, "input closed by default");
    require(session.openInput().success, "input queue starts");
    session.setAccepting(true);
    source.emitBlock();
    require(session.flushInput(std::chrono::seconds{2}).success, "drain input");
    require(pipeline.flushProcessing(std::chrono::seconds{2}).success, "drain processing");
    require(session.metrics().ingestedBlocks == 1
                && pipeline.metricsSnapshot().processedSamples == 1,
            "source reaches processing with no waterfall controller");
    session.setAccepting(false);
    source.emitBlock();
    require(session.metrics().receivedBlocks == 1, "disabled input excludes new blocks");
    session.closeInput(true);
    require(session.openInput().success, "input can reopen for another recording");
    session.setAccepting(true);
    source.emitBlock();
    session.closeInput(true);
    require(session.metrics().ingestedBlocks == 1, "reopened queue is drained on close");
    require(session.stopSource().success && session.stopSource().success && source.stops == 1,
            "source stop is idempotent");
    require(!session.sourceActive(), "source activity reflects stop");
}

void testFailuresAndTeardown()
{
    Source source;
    pipeline::DataIngestPipeline pipeline(config());
    {
        pipeline::BcoAcquisitionSession missing(nullptr, &pipeline);
        require(!missing.startSource(), "missing source rejected");
        pipeline::BcoAcquisitionSession noPipeline(&source, nullptr);
        require(!noPipeline.startSource() && !noPipeline.openInput(), "missing pipeline rejected");
    }
    {
        pipeline::BcoAcquisitionSession session(&source, &pipeline);
        source.failStart = true;
        require(!session.startSource() && !session.sourceActive(), "start failure propagated");
        source.failStart = false;
        require(session.openInput().success, "queue opens after failed start");
        session.setAccepting(true);
        require(session.startSource().success, "start retry succeeds");
        source.emitDuringStop = true;
        // Destructor must close the gate before joining the source, even if stop
        // delivers a final callback synchronously. Pipeline outlives the session.
    }
    require(source.stops == 1 && !source.callback, "destructor joins source and releases callback");
    require(pipeline.metricsSnapshot().inputSamples == 0, "teardown excludes final callback");
}

void testThreadedGeneratorDelivery()
{
    hardware::SimulatorBcoLoadConfig load;
    load.samplesPerSecond = 1000;
    load.batchPeriod = std::chrono::milliseconds{10};
    hardware::HighLoadSimulatorBcoStreamSource source(load);
    hardware::BcoStreamConfig stream;
    stream.bandConfigs = {*core::BandConfig::create(0, 3'000'000'000LL, 500'000'000LL).value()};
    require(source.configure(stream).success, "threaded generator configures");
    pipeline::DataIngestPipeline pipeline(config());
    pipeline::BcoAcquisitionSession session(&source, &pipeline);
    require(session.openInput().success, "threaded input opens");
    session.setAccepting(true);
    require(session.startSource().success, "threaded source starts");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (source.metrics().producedBatches < 3 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    require(session.stopSource().success, "stop joins actual generator worker");
    session.closeInput(true);
    require(pipeline.flushProcessing(std::chrono::seconds{2}).success, "threaded processing drains");
    const auto produced = source.metrics();
    const auto received = session.metrics();
    const auto processed = pipeline.metricsSnapshot();
    require(produced.producedBatches >= 3 && produced.producedSamples > 0,
            "actual generator delivers several blocks");
    require(received.ingestedBlocks == produced.producedBatches
                && received.droppedBlocks == 0 && received.rejectedBlocks == 0
                && processed.processedSamples == produced.producedSamples,
            "all actual generator callbacks reach processing before teardown");
}

void testSourceSwitchRequiresInactiveClosedInput()
{
    Source first;
    Source second;
    pipeline::DataIngestPipeline pipeline(config());
    pipeline::BcoAcquisitionSession session(&first, &pipeline);
    require(session.startSource().success, "first source starts");
    require(!session.setSource(&second), "active source cannot be replaced");
    require(session.openInput().success, "first input opens");
    session.setAccepting(true);
    first.emitBlock();
    require(session.flushInput(std::chrono::seconds{2}).success, "first source drains");
    require(session.stopSource().success, "first source stops before switching");
    require(!session.setSource(&second), "open input prevents replacing stopped source");
    session.closeInput(true);
    require(session.setSource(&second).success, "closed inactive session accepts replacement");
    require(session.openInput().success, "replacement input opens");
    session.setAccepting(true);
    require(session.startSource().success, "replacement source starts");
    first.emitBlock();
    second.emitBlock();
    require(session.flushInput(std::chrono::seconds{2}).success, "replacement source drains");
    require(session.metrics().receivedBlocks == 1 && first.starts == 1 && second.starts == 1,
            "only replacement source delivers to new recording");
    require(session.stopSource().success, "replacement source stops");
    session.closeInput(true);
    require(first.stops == 1 && second.stops == 1 && !first.callback && !second.callback,
            "switching leaves no old callbacks");
    require(session.setSource(nullptr).success, "inactive session permits disabling acquisition");
    require(!session.startSource() && !session.sourceActive(), "disabled acquisition cannot start");
    require(session.setSource(&first).success, "disabled session can restore built-in source");
    require(session.startSource().success && first.starts == 2, "restored source restarts");
    require(session.stopSource().success, "restored source stops");
}

} // namespace

int main()
{
    testHeadlessDeliveryAndRecordingGate();
    testFailuresAndTeardown();
    testThreadedGeneratorDelivery();
    testSourceSwitchRequiresInactiveClosedInput();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
