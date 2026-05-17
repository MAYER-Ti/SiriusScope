#include "app/bearingframebus.h"
#include "app/frequencyviewportmodel.h"
#include "app/waterfallcontroller.h"
#include "app/waterfallringbuffer.h"
#include "core/domain_models.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
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
        m_running = true;
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        m_running = false;
        return core::OperationResult::ok();
    }

    void emitBatch(const hardware::BcoSampleBatch& batch)
    {
        if (m_running && m_callback) {
            m_callback(batch);
        }
    }

private:
    SampleBatchCallback m_callback;
    bool m_running = false;
};

class RecordingDiagnosticsSink final : public infrastructure::IDiagnosticsSink
{
public:
    void publish(const infrastructure::DiagnosticEvent& event) override
    {
        std::lock_guard lock(m_mutex);
        events.push_back(event);
    }

    bool contains(const std::string& text) const
    {
        std::lock_guard lock(m_mutex);
        return std::any_of(events.cbegin(), events.cend(), [&text](const auto& event) {
            return event.message.find(text) != std::string::npos;
        });
    }

    mutable std::mutex m_mutex;
    std::vector<infrastructure::DiagnosticEvent> events;
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

core::SignalSample makeSample(const std::vector<core::BandConfig>& bands,
                              std::uint64_t sampleIndex,
                              int bandIndex,
                              int beamIndex,
                              int amplitude)
{
    const auto& band = bands[static_cast<std::size_t>(bandIndex)];
    const auto created = core::SignalSample::create(
        core::BeamSample{sampleIndex, 0, amplitude, beamIndex},
        band);
    return *created.value();
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 1500)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (predicate()) {
            return true;
        }
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return predicate();
}

bool emitRenderableRow(FakeSampleSource& source,
                       WaterfallRingBuffer* buffer,
                       const std::vector<core::BandConfig>& bands,
                       std::uint64_t sampleIndex)
{
    if (!buffer) {
        return false;
    }

    const std::uint64_t targetWriteIndex = buffer->writeIndex() + 1;
    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, sampleIndex, 0, 0, 90),
                                               makeSample(bands, sampleIndex, 0, 1, 40)}});

    return waitUntil([buffer, targetWriteIndex] {
        return buffer->writeIndex() >= targetWriteIndex;
    });
}

void processEventsFor(int durationMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
}

void testControllerStartsWithRecordingDisabled(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    config.sourceFlushIntervalMs = 20;

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config);

    test.require(!controller.sessionActive(), "recording is disabled on controller startup");
    test.require(!controller.liveMode(), "startup is not live without an active session");
    test.require(controller.recordingStatusText() == QStringLiteral("выключена"),
                 "startup status text reports disabled recording");
}

void testInactiveSessionIgnoresRenderableRows(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    config.sourceFlushIntervalMs = 20;

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config);
    controller.start();

    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const std::uint64_t initialWriteIndex = buffer ? buffer->writeIndex() : 0;

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 1, 0, 0, 90),
                                               makeSample(bands, 1, 0, 1, 40)}});
    processEventsFor(120);

    test.require(buffer != nullptr, "controller exposes a waterfall ring buffer");
    test.require(buffer && buffer->writeIndex() == initialWriteIndex,
                 "inactive recording ignores incoming render rows");
    test.require(storage.listSessions().isEmpty(),
                 "inactive recording does not create implicit sessions");
}

void testInputBatchUpdatesModel(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 128;
    config.visibleRowCount = 16;
    config.sourceFlushIntervalMs = 20;

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config);
    controller.start();
    controller.startRecording();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 1, 0, 0, 90),
                                               makeSample(bands, 1, 0, 1, 40)}});

    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const bool updated = waitUntil([buffer] {
        return buffer && buffer->populatedRows() > 0;
    });

    QVector<WaterfallBeamBin> copied(buffer ? buffer->nbins() : 0);
    const bool copiedLine = buffer && buffer->copyLine(0, copied.data(), copied.size());
    const bool hasSignal = std::any_of(copied.cbegin(), copied.cend(), [](const auto& bin) {
        return bin.left > 0 || bin.right > 0;
    });

    test.require(updated, "controller appends a render row from input batch");
    test.require(copiedLine && hasSignal, "controller updates ring buffer with non-zero samples");
    test.require(controller.sessionActive(), "startRecording enables the active session state");
    test.require(!storage.listSessions().isEmpty(), "startRecording creates an in-memory session");
}

void testProcessingPublishesBearingFrames(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    app::BearingFrameBus bearingFrameBus;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 128;
    config.visibleRowCount = 16;
    config.sourceFlushIntervalMs = 20;

    std::mutex mutex;
    int receivedFrameCount = 0;
    bearingFrameBus.subscribe([&](std::vector<processing::BearingInputFrame> frames) {
        std::lock_guard lock(mutex);
        receivedFrameCount += static_cast<int>(frames.size());
    });

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config,
                                                     &bearingFrameBus);
    controller.start();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 1, 0, 0, 90),
                                               makeSample(bands, 1, 0, 1, 40)}});

    const bool published = waitUntil([&] {
        std::lock_guard lock(mutex);
        return receivedFrameCount > 0;
    });

    test.require(published, "WaterfallController publishes bearing frames to bus");
}

void testFlushProcessingDrainsQueuedBatches(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    app::BearingFrameBus bearingFrameBus;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 128;
    config.visibleRowCount = 16;
    config.sourceFlushIntervalMs = 60'000;

    std::mutex mutex;
    int receivedFrameCount = 0;
    bearingFrameBus.subscribe([&](std::vector<processing::BearingInputFrame> frames) {
        std::lock_guard lock(mutex);
        receivedFrameCount += static_cast<int>(frames.size());
    });

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config,
                                                     &bearingFrameBus);
    controller.start();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 10, 0, 0, 90),
                                               makeSample(bands, 10, 0, 1, 40)}});
    const auto flushResult = controller.flushProcessing(std::chrono::milliseconds{1500});

    const bool published = waitUntil([&] {
        std::lock_guard lock(mutex);
        return receivedFrameCount > 0;
    });

    test.require(flushResult.success, "flushProcessing returns success");
    test.require(published, "flushProcessing drains queued batches into bearing frames");
}

void testScrollHistoryNoopDoesNotRebuildEmptyHistory(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    config.sourceFlushIntervalMs = 20;

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config);

    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const std::uint64_t initialWriteIndex = buffer ? buffer->writeIndex() : 0;
    const std::uint64_t initialGenerationId = buffer ? buffer->generationId() : 0;

    controller.scrollHistory(1);
    controller.scrollHistory(-1);
    controller.jumpToLive();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

    test.require(buffer != nullptr, "controller exposes a waterfall ring buffer");
    test.require(buffer && buffer->writeIndex() == initialWriteIndex,
                 "empty-history scroll does not rebuild render buffer");
    test.require(buffer && buffer->generationId() == initialGenerationId,
                 "empty-history scroll does not advance generation id");
}

void testScrollHistoryRebuildsOnlyWhenWindowChanges(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 1;
    config.sourceFlushIntervalMs = 5;

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config);
    controller.start();
    controller.startRecording();

    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const bool rowsReady =
        emitRenderableRow(source, buffer, bands, 1)
        && emitRenderableRow(source, buffer, bands, 2)
        && emitRenderableRow(source, buffer, bands, 3);

    const std::uint64_t beforeScroll = buffer ? buffer->writeIndex() : 0;
    controller.scrollHistory(1000);
    const bool olderRebuilt = waitUntil([buffer, beforeScroll] {
        return buffer && buffer->writeIndex() >= beforeScroll + 1;
    });
    const std::uint64_t afterOlderScroll = buffer ? buffer->writeIndex() : 0;
    controller.scrollHistory(1);
    processEventsFor(50);
    const std::uint64_t afterOldestBoundary = buffer ? buffer->writeIndex() : 0;
    controller.scrollHistory(-1000);
    const bool liveRebuilt = waitUntil([buffer, afterOldestBoundary] {
        return buffer && buffer->writeIndex() >= afterOldestBoundary + 1;
    });
    const std::uint64_t afterLiveScroll = buffer ? buffer->writeIndex() : 0;
    controller.scrollHistory(-1);
    processEventsFor(50);
    const std::uint64_t afterLiveBoundary = buffer ? buffer->writeIndex() : 0;

    test.require(rowsReady, "controller appends enough rows for history scrolling");
    test.require(olderRebuilt && afterOlderScroll == beforeScroll + 1,
                 "older-history scroll rebuilds render buffer once");
    test.require(afterOldestBoundary == afterOlderScroll,
                 "oldest-boundary scroll does not rebuild render buffer");
    test.require(liveRebuilt && afterLiveScroll == afterOldestBoundary + 1,
                 "scrolling back to live rebuilds render buffer once");
    test.require(afterLiveBoundary == afterLiveScroll,
                 "live-boundary scroll does not rebuild render buffer");
    test.require(controller.liveMode(), "negative scroll returns controller to live mode");
}

void testEmptyBatchDoesNotCrashAndReportsDiagnostic(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    config.sourceFlushIntervalMs = 20;

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config);
    controller.start();

    source.emitBatch(hardware::BcoSampleBatch{});

    const bool diagnosed = waitUntil([&diagnostics] {
        return diagnostics.contains("sample batch is empty");
    });

    test.require(diagnosed, "empty batch is routed to processing diagnostics");
}

void testStopRecordingFreezesWaterfallFlow(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    config.sourceFlushIntervalMs = 20;

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config);
    controller.start();
    controller.startRecording();

    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const bool firstRowReady = emitRenderableRow(source, buffer, bands, 1);
    const std::uint64_t beforeStopWriteIndex = buffer ? buffer->writeIndex() : 0;

    controller.stopRecording();
    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 2, 0, 0, 90),
                                               makeSample(bands, 2, 0, 1, 40)}});
    processEventsFor(120);

    test.require(firstRowReady, "active recording accepts a render row before stop");
    test.require(!controller.sessionActive(), "stopRecording disables the active session state");
    test.require(buffer && buffer->writeIndex() == beforeStopWriteIndex,
                 "stopped recording does not move the waterfall on new input");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testControllerStartsWithRecordingDisabled(test);
    testInactiveSessionIgnoresRenderableRows(test);
    testInputBatchUpdatesModel(test);
    testProcessingPublishesBearingFrames(test);
    testFlushProcessingDrainsQueuedBatches(test);
    testScrollHistoryNoopDoesNotRebuildEmptyHistory(test);
    testScrollHistoryRebuildsOnlyWhenWindowChanges(test);
    testEmptyBatchDoesNotCrashAndReportsDiagnostic(test);
    testStopRecordingFreezesWaterfallFlow(test);

    return test.result();
}
