#include "app/bearingframebus.h"
#include "app/frequencyviewportmodel.h"
#include "app/signalsamplebus.h"
#include "app/spectrumenvelopecontroller.h"
#include "app/spectrumenvelopeworker.h"
#include "app/waterfallcontroller.h"
#include "app/waterfallringbuffer.h"
#include "core/domain_models.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <algorithm>
#include <atomic>
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
        ++m_startCount;
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

    int startCount() const noexcept { return m_startCount; }

private:
    SampleBatchCallback m_callback;
    bool m_running = false;
    int m_startCount = 0;
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

class CountingWaterfallSessionStorage final : public IWaterfallSessionStorage
{
public:
    QVector<WaterfallSessionMetadata> listSessions() const override
    {
        return m_delegate.listSessions();
    }

    std::optional<WaterfallSessionMetadata> session(const WaterfallSessionId& id) const override
    {
        return m_delegate.session(id);
    }

    std::optional<WaterfallSessionMetadata> latestSession() const override
    {
        return m_delegate.latestSession();
    }

    std::optional<WaterfallSessionMetadata> previousSession(
        const WaterfallSessionId& id) const override
    {
        return m_delegate.previousSession(id);
    }

    std::optional<WaterfallSessionMetadata> nextSession(
        const WaterfallSessionId& id) const override
    {
        return m_delegate.nextSession(id);
    }

    WaterfallSessionMetadata startSession(WaterfallSessionMetadata metadata) override
    {
        startSessionCount.fetch_add(1, std::memory_order_relaxed);
        return m_delegate.startSession(std::move(metadata));
    }

    bool closeSession(const WaterfallSessionId& id, qint64 endUtcMs) override
    {
        closeSessionCount.fetch_add(1, std::memory_order_relaxed);
        return m_delegate.closeSession(id, endUtcMs);
    }

    void appendRow(const WaterfallSessionId& id, const WaterfallRow& row) override
    {
        appendRowCount.fetch_add(1, std::memory_order_relaxed);
        m_delegate.appendRow(id, row);
    }

    QVector<WaterfallRow> loadRows(const WaterfallSessionId& id,
                                   qint64 fromUtcMs,
                                   qint64 toUtcMs,
                                   int maxRows) const override
    {
        loadRowsCount.fetch_add(1, std::memory_order_relaxed);
        return m_delegate.loadRows(id, fromUtcMs, toUtcMs, maxRows);
    }

    int rowCount(const WaterfallSessionId& id) const override
    {
        return m_delegate.rowCount(id);
    }

    void resetCounts()
    {
        appendRowCount.store(0, std::memory_order_relaxed);
        loadRowsCount.store(0, std::memory_order_relaxed);
        startSessionCount.store(0, std::memory_order_relaxed);
        closeSessionCount.store(0, std::memory_order_relaxed);
    }

    mutable std::atomic<int> appendRowCount{0};
    mutable std::atomic<int> loadRowsCount{0};
    mutable std::atomic<int> startSessionCount{0};
    mutable std::atomic<int> closeSessionCount{0};

private:
    InMemoryWaterfallSessionStorage m_delegate;
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

double maxEnvelopeSample(const app::SpectrumEnvelopeController& controller)
{
    const QVariantList samples = controller.envelopeSamples();
    double maxValue = 0.0;
    for (const auto& sample : samples) {
        maxValue = std::max(maxValue, sample.toDouble());
    }
    return maxValue;
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
    controller.startLiveSource();

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
    controller.startLiveSource();
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

void testLiveAppendBypassesHistoryReload(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    CountingWaterfallSessionStorage storage;
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
    controller.startLiveSource();

    const bool initialHistoryLoadFinished = waitUntil([&controller] {
        return !controller.historyLoading();
    });
    test.require(initialHistoryLoadFinished, "initial recording history load finishes");
    test.require(storage.startSessionCount.load(std::memory_order_relaxed) == 1,
                 "startRecording creates one counted session");

    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const std::uint64_t initialWriteIndex = buffer ? buffer->writeIndex() : 0;
    storage.resetCounts();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 1, 0, 0, 90),
                                               makeSample(bands, 1, 0, 1, 40)}});

    const bool liveRowReady = waitUntil([&storage, buffer, initialWriteIndex] {
        return storage.appendRowCount.load(std::memory_order_relaxed) == 1
            && buffer
            && buffer->writeIndex() >= initialWriteIndex + 1;
    });
    processEventsFor(120);

    QVector<WaterfallBeamBin> copied(buffer ? buffer->nbins() : 0);
    const bool copiedLine =
        buffer && buffer->copyLine(0, copied.data(), static_cast<int>(copied.size()));
    const bool hasSignal = std::any_of(copied.cbegin(), copied.cend(), [](const auto& bin) {
        return bin.left > 0 || bin.right > 0;
    });

    test.require(liveRowReady, "live append pushes a row into the ring buffer");
    test.require(storage.appendRowCount.load(std::memory_order_relaxed) == 1,
                 "live append still writes one row to storage");
    test.require(storage.loadRowsCount.load(std::memory_order_relaxed) == 0,
                 "live append does not reload rows through storage history");
    test.require(buffer && buffer->writeIndex() == initialWriteIndex + 1,
                 "live append advances the ring buffer exactly once");
    test.require(copiedLine && hasSignal,
                 "live append writes non-zero signal data into the top buffer row");
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
    controller.startRecording();
    controller.startLiveSource();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 1, 0, 0, 90),
                                               makeSample(bands, 1, 0, 1, 40)}});

    const bool published = waitUntil([&] {
        std::lock_guard lock(mutex);
        return receivedFrameCount > 0;
    });

    test.require(published, "WaterfallController publishes bearing frames to bus");
}

void testProcessingPublishesSignalSamples(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeSampleSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    app::SignalSampleBus signalSampleBus;
    const auto bands = makeBandConfigs();
    siriusscope::app::WaterfallControllerConfig config;
    config.renderBinCount = 128;
    config.visibleRowCount = 16;
    config.sourceFlushIntervalMs = 20;

    std::mutex mutex;
    std::vector<core::SignalSample> receivedSamples;
    signalSampleBus.subscribe([&](std::vector<core::SignalSample> samples) {
        std::lock_guard lock(mutex);
        receivedSamples = std::move(samples);
    });

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config,
                                                     nullptr,
                                                     &signalSampleBus);
    controller.start();
    controller.startRecording();
    controller.startLiveSource();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 1, 0, 0, 90),
                                               makeSample(bands, 1, 0, 1, 40)}});

    const bool published = waitUntil([&] {
        std::lock_guard lock(mutex);
        return receivedSamples.size() == 2;
    });

    test.require(published, "WaterfallController publishes raw signal samples to bus");
    {
        std::lock_guard lock(mutex);
        test.require(receivedSamples.size() == 2 && receivedSamples[0].sampleIndex == 1
                         && receivedSamples[1].sampleIndex == 1,
                     "SignalSampleBus receives the original sample payload");
    }
}

void testInputBatchUpdatesSpectrumEnvelope(TestRunner& test)
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

    siriusscope::app::SpectrumEnvelopeControllerConfig envelopeConfig;
    envelopeConfig.binCount = 128;
    envelopeConfig.publishIntervalMs = 1;
    siriusscope::app::SpectrumEnvelopeController envelope(envelopeConfig);
    siriusscope::app::SpectrumEnvelopeWorkerConfig workerConfig;
    workerConfig.processor.binCount = 128;
    workerConfig.processor.decayPerSecond = 0.0;
    workerConfig.publishIntervalMs = 1;
    siriusscope::app::SpectrumEnvelopeWorker envelopeWorker(workerConfig, &diagnostics);
    QObject::connect(&envelopeWorker,
                     &siriusscope::app::SpectrumEnvelopeWorker::envelopeSnapshotReady,
                     &envelope,
                     &siriusscope::app::SpectrumEnvelopeController::acceptSnapshot);
    envelopeWorker.setViewport(viewport.viewMinHz(), viewport.viewMaxHz());

    siriusscope::app::WaterfallController controller(&viewport,
                                                     &source,
                                                     bands,
                                                     &storage,
                                                     &diagnostics,
                                                     config,
                                                     nullptr,
                                                     nullptr,
                                                     &envelopeWorker);
    controller.start();
    controller.startRecording();
    controller.startLiveSource();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 1, 0, 0, 90),
                                               makeSample(bands, 1, 0, 1, 40)}});

    const bool updated = waitUntil([&envelope] {
        return maxEnvelopeSample(envelope) == 90.0;
    });

    test.require(updated, "WaterfallController forwards input batch to spectrum envelope");
    test.require(source.startCount() == 1, "BCO sample source is started once");
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
    controller.startRecording();
    controller.startLiveSource();

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

void testFlushProcessingAsyncDrainsQueuedBatches(TestRunner& test)
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
    controller.startRecording();
    controller.startLiveSource();

    source.emitBatch(hardware::BcoSampleBatch{{makeSample(bands, 10, 0, 0, 90),
                                               makeSample(bands, 10, 0, 1, 40)}});

    bool callbackCalled = false;
    core::OperationResult callbackResult;
    controller.flushProcessingAsync(std::chrono::milliseconds{1500},
                                    [&](core::OperationResult result) {
                                        callbackResult = std::move(result);
                                        callbackCalled = true;
                                    });

    const bool completed = waitUntil([&] {
        std::lock_guard lock(mutex);
        return callbackCalled && receivedFrameCount > 0;
    });

    test.require(completed, "flushProcessingAsync drains queued batches and calls callback");
    test.require(callbackResult.success, "flushProcessingAsync callback reports success");
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
    controller.startLiveSource();

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
    controller.startRecording();
    controller.startLiveSource();

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
    controller.startLiveSource();
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
    testLiveAppendBypassesHistoryReload(test);
    testProcessingPublishesBearingFrames(test);
    testProcessingPublishesSignalSamples(test);
    testInputBatchUpdatesSpectrumEnvelope(test);
    testFlushProcessingDrainsQueuedBatches(test);
    testFlushProcessingAsyncDrainsQueuedBatches(test);
    testScrollHistoryNoopDoesNotRebuildEmptyHistory(test);
    testScrollHistoryRebuildsOnlyWhenWindowChanges(test);
    testEmptyBatchDoesNotCrashAndReportsDiagnostic(test);
    testStopRecordingFreezesWaterfallFlow(test);

    return test.result();
}
