#include "app/bearingframebus.h"
#include "app/frequencyviewportmodel.h"
#include "app/signalsamplebus.h"
#include "app/spectrumenvelopecontroller.h"
#include "app/spectrumenvelopeworker.h"
#include "app/waterfallcontroller.h"
#include "app/waterfallringbuffer.h"
#include "core/domain_models.h"
#include "hardware/interfaces/bco_stream_source.h"
#include "pipeline/data_ingest_pipeline.h"

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
#include <limits>
#include <memory>
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

class FakeBcoStreamSource final : public hardware::IBcoStreamSource
{
public:
    core::OperationResult configure(const hardware::BcoStreamConfig&) override
    {
        return core::OperationResult::ok();
    }

    core::OperationResult start(SampleBlockCallback callback) override
    {
        m_callback = std::move(callback);
        m_running = true;
        ++m_startCount;
        return core::OperationResult::ok();
    }

    core::OperationResult stop() override
    {
        m_running = false;
        ++m_stopCount;
        return core::OperationResult::ok();
    }

    hardware::BcoSourceMetrics metrics() const override
    {
        return {};
    }

    void emitBlock(SampleBlockPtr block)
    {
        if (m_running && m_callback) {
            m_callback(std::move(block));
        }
    }

    int startCount() const noexcept { return m_startCount; }
    int stopCount() const noexcept { return m_stopCount; }
    bool running() const noexcept { return m_running; }

private:
    SampleBlockCallback m_callback;
    bool m_running = false;
    int m_startCount = 0;
    int m_stopCount = 0;
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

hardware::IBcoStreamSource::SampleBlockPtr makeBlock(
    std::vector<core::SignalSample> samples)
{
    auto block = std::make_shared<hardware::BcoSampleBlock>();
    block->samples = std::move(samples);
    block->stats.sampleCount = static_cast<std::uint64_t>(block->samples.size());
    block->stats.producedAt = std::chrono::steady_clock::now();
    if (!block->samples.empty()) {
        block->stats.firstSampleIndex = block->samples.front().sampleIndex;
        block->stats.lastSampleIndex = block->samples.back().sampleIndex;
    }
    return block;
}

pipeline::DataIngestPipelineConfig makePipelineConfig()
{
    return pipeline::DataIngestPipelineConfig{
        pipeline::SignalBlockPoolConfig{8, 1024},
        4,
        std::chrono::milliseconds{10},
        false,
    };
}

app::WaterfallControllerConfig makeLiveGapControllerConfig(int visibleRowCount = 10)
{
    app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = visibleRowCount;
    config.rowPeriodMs = 20;
    config.maxWaterfallRowsPerUiTick = 256;
    return config;
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

void startLiveWaterfall(app::WaterfallController& controller,
                        FakeBcoStreamSource& source)
{
    controller.start();
    controller.setWaterfallTimeBase(core::TimeBase{1'000'000'000, 0, 1'000'000});
    controller.startRecording();
    controller.startLiveSource();
    waitUntil([&controller] {
        return !controller.historyLoading();
    });
}

void emitWaterfallRows(FakeBcoStreamSource& source,
                       const std::vector<core::BandConfig>& bands,
                       const std::vector<std::pair<std::uint64_t, int>>& rows)
{
    std::vector<core::SignalSample> samples;
    samples.reserve(rows.size() * 2);
    for (const auto& [sampleIndex, amplitude] : rows) {
        samples.push_back(makeSample(bands, sampleIndex, 0, 0, amplitude));
        samples.push_back(makeSample(bands, sampleIndex, 0, 1, std::max(1, amplitude / 2)));
    }
    source.emitBlock(makeBlock(std::move(samples)));
}

QVector<WaterfallBeamBin> copyBufferLine(WaterfallRingBuffer* buffer, int row)
{
    if (!buffer) {
        return {};
    }

    QVector<WaterfallBeamBin> line(buffer->nbins());
    if (!buffer->copyLine(row, line.data(), line.size())) {
        return {};
    }
    return line;
}

bool lineHasSignal(const QVector<WaterfallBeamBin>& line)
{
    return std::any_of(line.cbegin(), line.cend(), [](const auto& bin) {
        return bin.left > 0 || bin.right > 0;
    });
}

void testStartLiveSourceStartsStreamSource(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        app::WaterfallControllerConfig{},
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    const auto result = controller.startLiveSource();

    test.require(result.success, "startLiveSource succeeds with stream source");
    test.require(source.startCount() == 1, "startLiveSource starts stream source once");
    test.require(source.running(), "stream source is running after startLiveSource");
    test.require(controller.sourceActive(), "controller reports active stream source");
    test.require(dataPipeline.running(), "startLiveSource starts the data ingest pipeline");
}

void testStopLiveSourceStopsStreamSource(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        app::WaterfallControllerConfig{},
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    const auto startResult = controller.startLiveSource();
    const auto stopResult = controller.stopLiveSource();

    test.require(startResult.success, "startLiveSource succeeds before stop test");
    test.require(stopResult.success, "stopLiveSource succeeds with stream source");
    test.require(source.stopCount() == 1, "stopLiveSource stops stream source once");
    test.require(!source.running(), "stream source is not running after stopLiveSource");
    test.require(!controller.sourceActive(), "controller reports inactive stream source");
}

void testNullStreamSourceReturnsFailure(TestRunner& test)
{
    FrequencyViewportModel viewport;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    const auto bands = makeBandConfigs();

    app::WaterfallController controller(&viewport,
                                        nullptr,
                                        bands,
                                        &storage,
                                        &diagnostics);

    const auto result = controller.startLiveSource();

    test.require(!result, "startLiveSource fails without stream source");
    test.require(result.message == "waterfall stream source is not configured",
                 "startLiveSource reports missing stream source");
    test.require(diagnostics.contains("Waterfall stream source is not configured"),
                 "missing stream source is published as diagnostic");
}

void testSourceBlocksGoToDataPlane(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        config,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    controller.start();
    controller.startRecording();
    controller.startLiveSource();
    source.emitBlock(makeBlock({makeSample(bands, 1, 0, 0, 90),
                                makeSample(bands, 2, 0, 1, 40)}));

    const auto flushed = controller.flushProcessing(std::chrono::milliseconds{1500});
    const auto summary = dataPipeline.lastSummary();

    test.require(flushed.success, "flushProcessing delegates to data ingest pipeline");
    test.require(summary.processedBlocks == 1, "data plane processes source block");
    test.require(summary.processedSamples == 2, "data plane counts source samples");
}

void testSourceBlockIgnoredBeforeRecording(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        app::WaterfallControllerConfig{},
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    controller.start();
    controller.startLiveSource();
    source.emitBlock(makeBlock({makeSample(bands, 1, 0, 0, 90)}));

    const auto flushed = controller.flushProcessing(std::chrono::milliseconds{1500});
    const auto metrics = dataPipeline.metricsSnapshot();

    test.require(flushed.success, "flush before recording succeeds");
    test.require(metrics.inputSamples == 0,
                 "source block before startRecording is ignored by runtime bridge path");
    test.require(metrics.processedSamples == 0,
                 "ignored pre-recording source block is not processed");
}

void testStopRecordingFlushesRuntimeBridge(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        config,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    controller.start();
    controller.setWaterfallTimeBase(core::TimeBase{1'000'000'000, 0, 1'000'000});
    controller.startRecording();
    controller.startLiveSource();
    waitUntil([&controller] {
        return !controller.historyLoading();
    });

    source.emitBlock(makeBlock({makeSample(bands, 0, 0, 0, 90),
                                makeSample(bands, 0, 0, 1, 40)}));
    controller.stopRecording();

    const auto metrics = dataPipeline.metricsSnapshot();
    const auto latestSession = storage.latestSession();
    const auto rowCount = latestSession ? storage.rowCount(latestSession->id) : 0;

    test.require(metrics.inputSamples == 2,
                 "stopRecording flushes bridge samples into data pipeline");
    test.require(metrics.processedSamples == metrics.inputSamples,
                 "stopRecording flushes processing after bridge drain");
    test.require(metrics.droppedBlocks == 0 && metrics.droppedSamples == 0,
                 "runtime bridge stop path does not drop normal recording block");
    test.require(latestSession.has_value() && rowCount == 1,
                 "stopRecording stores row produced from bridge-delivered block");
    test.require(diagnostics.contains("Source bridge stopped: received=1"),
                 "stopRecording publishes source bridge summary diagnostics");
    test.require(!diagnostics.contains("Source bridge reported dropped/rejected blocks"),
                 "normal bridge stop does not publish dropped/rejected warning");
}

void testSourceBlocksUpdateWaterfallRingBufferThroughQueuedRows(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::WaterfallControllerConfig config;
    config.renderBinCount = 64;
    config.visibleRowCount = 8;
    config.maxWaterfallRowsPerUiTick = 256;
    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        config,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    controller.start();
    controller.setWaterfallTimeBase(core::TimeBase{1'000'000'000, 0, 1'000'000});
    controller.startRecording();
    controller.startLiveSource();
    waitUntil([&controller] {
        return !controller.historyLoading();
    });
    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const auto initialWriteIndex = buffer ? buffer->writeIndex() : 0;

    source.emitBlock(makeBlock({makeSample(bands, 0, 0, 0, 90),
                                makeSample(bands, 0, 0, 1, 40),
                                makeSample(bands, 20, 0, 0, 80),
                                makeSample(bands, 20, 0, 1, 35),
                                makeSample(bands, 40, 0, 0, 70),
                                makeSample(bands, 40, 0, 1, 30)}));
    controller.flushProcessing(std::chrono::milliseconds{1500});
    const bool snapshotApplied = waitUntil([&] {
        return buffer && buffer->writeIndex() >= initialWriteIndex + 3;
    });
    const auto latestSession = storage.latestSession();
    const auto storedRows = latestSession
        ? storage.loadRows(latestSession->id, 0, std::numeric_limits<qint64>::max(), 10)
        : QVector<WaterfallRow>{};

    test.require(buffer != nullptr, "controller exposes a waterfall ring buffer");
    test.require(snapshotApplied,
                 "high-load source block appends all queued waterfall rows");
    test.require(latestSession && storage.rowCount(latestSession->id) == 3,
                 "all queued rows are stored as aggregated waterfall rows");
    test.require(latestSession && latestSession->rowPeriodMs == config.rowPeriodMs,
                 "waterfall session metadata preserves controller row period");
    test.require(storedRows.size() == 3
                     && storedRows[0].utcMs == 1000
                     && storedRows[1].utcMs == 1020
                     && storedRows[2].utcMs == 1040,
                 "queued row utc timing is preserved in storage");
    test.require(storedRows.size() == 3
                     && storedRows[0].firstSampleIndex == 0
                     && storedRows[1].firstSampleIndex == 20
                     && storedRows[2].firstSampleIndex == 40,
                 "queued row sample index ranges are preserved in storage");
    if (latestSession && storedRows.size() == 3) {
        WaterfallTimelineMapper mapper(storedRows[2].utcMs,
                                       latestSession->rowPeriodMs,
                                       config.visibleRowCount,
                                       config.visibleRowCount);
        test.require(mapper.rowIndexForUtcMs(storedRows[2].utcMs) == 0
                         && mapper.rowIndexForUtcMs(storedRows[1].utcMs) == 1
                         && mapper.rowIndexForUtcMs(storedRows[0].utcMs) == 2,
                     "history timeline maps stored rows to matching row slots");
    }
}

void testLiveInsertsEmptyRowsForTimeGaps(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();
    const auto config = makeLiveGapControllerConfig();

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        config,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    startLiveWaterfall(controller, source);
    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const auto initialWriteIndex = buffer ? buffer->writeIndex() : 0;

    emitWaterfallRows(source, bands, {{0, 90}, {100, 70}});
    const auto flushed = controller.flushProcessing(std::chrono::milliseconds{1500});
    const bool rowsApplied = waitUntil([&] {
        return buffer && buffer->writeIndex() >= initialWriteIndex + 6;
    });

    test.require(flushed.success, "live gap test flush succeeds");
    test.require(buffer != nullptr, "live gap test has ring buffer");
    test.require(rowsApplied, "live gap rows are inserted into ring buffer");
    test.require(buffer && buffer->populatedRows() >= 6,
                 "live gap rows populate visible waterfall slots");

    if (buffer && rowsApplied) {
        test.require(lineHasSignal(copyBufferLine(buffer, 0)),
                     "newest live row remains at the top after gap filling");
        for (int row = 1; row <= 4; ++row) {
            test.require(!lineHasSignal(copyBufferLine(buffer, row)),
                         "missing live time slot is rendered as an empty row");
        }
        test.require(lineHasSignal(copyBufferLine(buffer, 5)),
                     "previous live row remains below inserted gap rows");
    }
}

void testLiveAdjacentRowsDoNotInsertGaps(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();
    const auto config = makeLiveGapControllerConfig();

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        config,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    startLiveWaterfall(controller, source);
    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const auto initialWriteIndex = buffer ? buffer->writeIndex() : 0;

    emitWaterfallRows(source, bands, {{0, 90}, {20, 70}});
    const auto flushed = controller.flushProcessing(std::chrono::milliseconds{1500});
    const bool rowsApplied = waitUntil([&] {
        return buffer && buffer->writeIndex() >= initialWriteIndex + 2;
    });

    test.require(flushed.success, "adjacent live row flush succeeds");
    test.require(rowsApplied, "adjacent live rows reach ring buffer");
    test.require(buffer && buffer->writeIndex() == initialWriteIndex + 2,
                 "adjacent live rows advance ring buffer by real row count only");
}

void testLiveGapRowsAreNotStored(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();
    const auto config = makeLiveGapControllerConfig();

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        config,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    startLiveWaterfall(controller, source);
    emitWaterfallRows(source, bands, {{0, 90}, {100, 70}});
    controller.flushProcessing(std::chrono::milliseconds{1500});

    const auto latestSession = storage.latestSession();
    const auto storedRows = latestSession
        ? storage.loadRows(latestSession->id, 0, std::numeric_limits<qint64>::max(), 10)
        : QVector<WaterfallRow>{};

    test.require(latestSession.has_value(), "live gap storage test creates a session");
    test.require(latestSession && storage.rowCount(latestSession->id) == 2,
                 "storage keeps only real waterfall rows across a live gap");
    test.require(storedRows.size() == 2
                     && storedRows[0].utcMs == 1000
                     && storedRows[1].utcMs == 1100,
                 "stored live gap rows keep original utc timing without artificial rows");
}

void testHugeLiveGapIsClamped(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();
    const auto config = makeLiveGapControllerConfig(10);

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        config,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    startLiveWaterfall(controller, source);
    auto* buffer = qobject_cast<WaterfallRingBuffer*>(controller.ringBuffer());
    const auto initialWriteIndex = buffer ? buffer->writeIndex() : 0;
    const auto maxExpectedAdvance =
        buffer ? static_cast<std::uint64_t>(buffer->height() + 1) : 0;

    emitWaterfallRows(source, bands, {{0, 90}, {100000, 70}});
    const auto flushed = controller.flushProcessing(std::chrono::milliseconds{1500});
    const bool rowsApplied = waitUntil([&] {
        return buffer && buffer->writeIndex() >= initialWriteIndex + maxExpectedAdvance;
    });
    const auto latestSession = storage.latestSession();

    test.require(flushed.success, "huge live gap flush succeeds");
    test.require(rowsApplied, "huge live gap is rendered without an unbounded loop");
    test.require(buffer && buffer->writeIndex() == initialWriteIndex + maxExpectedAdvance,
                 "huge live gap insertion is clamped to ring buffer height");
    test.require(latestSession && storage.rowCount(latestSession->id) == 2,
                 "huge live gap does not create artificial stored rows");
}

void testHighLoadPathDoesNotPublishRawBuses(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    app::BearingFrameBus bearingFrameBus;
    app::SignalSampleBus signalSampleBus;
    const auto bands = makeBandConfigs();

    std::atomic<int> bearingFrameCount{0};
    std::atomic<int> sampleCount{0};
    bearingFrameBus.subscribe([&](std::vector<processing::BearingInputFrame> frames) {
        bearingFrameCount += static_cast<int>(frames.size());
    });
    signalSampleBus.subscribe([&](std::vector<core::SignalSample> samples) {
        sampleCount += static_cast<int>(samples.size());
    });

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        app::WaterfallControllerConfig{},
                                        &bearingFrameBus,
                                        &signalSampleBus,
                                        nullptr,
                                        &dataPipeline);

    controller.start();
    controller.startRecording();
    controller.startLiveSource();
    source.emitBlock(makeBlock({makeSample(bands, 1, 0, 0, 90),
                                makeSample(bands, 1, 0, 1, 40)}));
    controller.flushProcessing(std::chrono::milliseconds{1500});
    processEventsFor(80);

    test.require(bearingFrameCount.load() == 0,
                 "high-load path does not publish bearing frames through bus");
    test.require(sampleCount.load() == 0,
                 "high-load path does not publish raw samples through bus");
}

void testHighLoadPathDoesNotCopyBlocksToSpectrumWorker(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::SpectrumEnvelopeControllerConfig envelopeConfig;
    envelopeConfig.binCount = 128;
    envelopeConfig.publishIntervalMs = 1;
    app::SpectrumEnvelopeController envelope(envelopeConfig);
    app::SpectrumEnvelopeWorkerConfig workerConfig;
    workerConfig.processor.binCount = 128;
    workerConfig.processor.decayPerSecond = 0.0;
    workerConfig.publishIntervalMs = 1;
    app::SpectrumEnvelopeWorker envelopeWorker(workerConfig, &diagnostics);
    QObject::connect(&envelopeWorker,
                     &app::SpectrumEnvelopeWorker::envelopeSnapshotReady,
                     &envelope,
                     &app::SpectrumEnvelopeController::acceptSnapshot);
    envelopeWorker.setViewport(viewport.viewMinHz(), viewport.viewMaxHz());

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        app::WaterfallControllerConfig{},
                                        nullptr,
                                        nullptr,
                                        &envelopeWorker,
                                        &dataPipeline);

    controller.start();
    controller.startRecording();
    controller.startLiveSource();
    source.emitBlock(makeBlock({makeSample(bands, 1, 0, 0, 90),
                                makeSample(bands, 1, 0, 1, 40)}));
    controller.flushProcessing(std::chrono::milliseconds{1500});
    processEventsFor(80);

    test.require(maxEnvelopeSample(envelope) == 0.0,
                 "high-load path does not copy source block into spectrum worker");
}

void testStopRecordingFreezesDataPlaneAcceptance(TestRunner& test)
{
    FrequencyViewportModel viewport;
    FakeBcoStreamSource source;
    RecordingDiagnosticsSink diagnostics;
    InMemoryWaterfallSessionStorage storage;
    pipeline::DataIngestPipeline dataPipeline(makePipelineConfig(), &diagnostics);
    const auto bands = makeBandConfigs();

    app::WaterfallController controller(&viewport,
                                        &source,
                                        bands,
                                        &storage,
                                        &diagnostics,
                                        app::WaterfallControllerConfig{},
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        &dataPipeline);

    controller.start();
    controller.startLiveSource();
    controller.startRecording();
    source.emitBlock(makeBlock({makeSample(bands, 1, 0, 0, 90)}));
    controller.flushProcessing(std::chrono::milliseconds{1500});
    const auto beforeStop = dataPipeline.lastSummary().processedSamples;

    controller.stopRecording();
    source.emitBlock(makeBlock({makeSample(bands, 2, 0, 0, 90)}));
    controller.flushProcessing(std::chrono::milliseconds{1500});
    const auto afterStop = dataPipeline.lastSummary().processedSamples;

    test.require(!controller.sessionActive(), "stopRecording disables active session");
    test.require(afterStop == beforeStop,
                 "data plane stops accepting source blocks after stopRecording");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testStartLiveSourceStartsStreamSource(test);
    testStopLiveSourceStopsStreamSource(test);
    testNullStreamSourceReturnsFailure(test);
    testSourceBlocksGoToDataPlane(test);
    testSourceBlockIgnoredBeforeRecording(test);
    testStopRecordingFlushesRuntimeBridge(test);
    testSourceBlocksUpdateWaterfallRingBufferThroughQueuedRows(test);
    testLiveInsertsEmptyRowsForTimeGaps(test);
    testLiveAdjacentRowsDoNotInsertGaps(test);
    testLiveGapRowsAreNotStored(test);
    testHugeLiveGapIsClamped(test);
    testHighLoadPathDoesNotPublishRawBuses(test);
    testHighLoadPathDoesNotCopyBlocksToSpectrumWorker(test);
    testStopRecordingFreezesDataPlaneAcceptance(test);

    return test.result();
}
