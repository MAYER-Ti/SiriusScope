#include "hardware/udp/udp_bco_stream_source.h"
#include "pipeline/bco_acquisition_session.h"
#include "pipeline/data_ingest_pipeline.h"
#include "bco_generator/protocol.h"
#include "bco_generator/udp_generator.h"
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <QProcess>
#include <QCoreApplication>

using namespace siriusscope;
namespace host = bco_generator::host;
namespace wire = bco_generator::wire;
using namespace std::chrono_literals;
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::cerr << message << '\n'; } }
template<class Predicate> bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(2ms);
    return predicate();
}
hardware::BcoStreamConfig streamConfig() {
    hardware::BcoStreamConfig c;
    c.bandConfigs = {*core::BandConfig::create(0, 3'000'000'000LL, 500'000'000LL).value()};
    c.timeBase.firstSampleIndex = 42; c.timeBase.samplePeriodNs = 320;
    c.timeBase.recordingStartUtcNs = 1'700'000'000'000'000'000LL;
    return c;
}
hardware::UdpBcoSourceConfig endpoint(std::uint16_t port) {
    hardware::UdpBcoSourceConfig c; c.generatorPort = port; c.bindHost = "127.0.0.1"; c.bindPort = 0; return c;
}
void generatedStreamReachesPipeline()
{
    host::UdpGenerator generator; host::GeneratorOptions options; options.port = 0;
    std::string error;
    if (!generator.open(options, error)) { check(false, error.c_str()); return; }
    std::atomic_bool stop{false};
    std::thread worker([&] { generator.run(stop); });
    hardware::UdpBcoStreamSource source(endpoint(generator.localPort()));
    check(source.configure(streamConfig()).success, "configure network source");
    pipeline::DataIngestPipelineConfig config; config.blockPool = {8, 8192}; config.queueCapacity = 8; config.acceptingOnStart = true;
    pipeline::DataIngestPipeline pipeline(config);
    pipeline::BcoAcquisitionSession session(&source, &pipeline);
    for (int recording = 0; recording < 2; ++recording) {
        check(session.openInput().success, "open pipeline"); session.setAccepting(true);
        check(session.startSource().success, "start UDP receiver");
        check(waitFor([&] { return source.metrics().producedSamples >= 100; }), "generator sends UDP data");
        session.stopSource(); session.closeInput(true);
        check(pipeline.flushProcessing(2s).success, "processing drains");
        const auto m = source.metrics();
        check(m.producedSamples > 0 && !m.malformedPackets && !m.lostPackets, "valid loopback data without packet loss");
        check(session.metrics().ingestedBlocks == m.producedBatches && session.metrics().droppedBlocks == 0,
              "UDP blocks pass acquisition bridge");
        check(pipeline.metricsSnapshot().processedSamples >= m.producedSamples, "UDP data reaches processing");
        std::this_thread::sleep_for(20ms); // server consumes Stop before next stream ID
    }
    stop = true; worker.join();
    check(generator.metrics().samples > 0 && !generator.metrics().sendErrors, "generator socket sends successfully");
}
void malformedLossAndBoundedPool()
{
    host::UdpSocket server; host::Endpoint bind; host::ipv4Endpoint("127.0.0.1", 0, bind);
    std::string error;
    if (!server.open(bind, error)) { check(false, error.c_str()); return; }
    hardware::UdpBcoStreamSource source(endpoint(server.localPort()));
    check(source.configure(streamConfig()).success, "configure injected source");
    std::mutex mutex;
    std::vector<hardware::IBcoStreamSource::SampleBlockPtr> held;
    check(source.start([&](auto block) { std::lock_guard lock(mutex); held.push_back(std::move(block)); }).success, "start injected source");
    std::array<std::byte, wire::maxDatagramBytes> buffer{};
    host::Endpoint peer; wire::Subscription request;
    const int size = server.receive(buffer, peer, 1000);
    if (size <= 0 || !wire::decodeSubscription(std::span(buffer).first(size > 0 ? size : 0), request)) {
        check(false, "receiver sends subscription"); source.stop(); return;
    }
    check(request.header.firstSampleIndex == 42 && request.header.startUtcNs == std::uint64_t(streamConfig().timeBase.recordingStartUtcNs),
          "receiver negotiates exact recording timebase");
    hardware::UdpBcoSourceConfig conflictEndpoint = endpoint(server.localPort()); conflictEndpoint.bindPort = source.localPort();
    hardware::UdpBcoStreamSource conflict(conflictEndpoint); conflict.configure(streamConfig());
    check(!conflict.start([](auto) {}), "bind conflict is reported synchronously");
    wire::Header h = request.header; h.producer = 77; h.sequence = 0;
    const std::array samples{bco_generator::Sample{42, 0, -80'000'000, 0, 80, 1}};
    auto send = [&] {
        auto encoded = wire::encodeData(h, samples, buffer);
        check(bool(encoded) && server.send(std::span(buffer).first(encoded.bytes), peer), "send injected valid packet");
    };
    send(); h.sequence = 2; send(); send(); h.sequence = 1; send(); // loss, duplicate, reorder
    h.sequence = 3; auto encoded = wire::encodeData(h, samples, buffer);
    buffer[94] = std::byte{0}; server.send(std::span(buffer).first(encoded.bytes), peer);
    ++h.stream; send(); --h.stream;
    check(waitFor([&] { const auto m = source.metrics(); return m.malformedPackets == 1 && source.networkMetrics().latePackets == 2; }),
          "malformed/duplicate/reordered datagrams accounted");
    check(source.metrics().lostPackets == 1 && source.networkMetrics().foreignPackets == 1, "loss and foreign session accounted");
    {
        std::lock_guard lock(mutex);
        check(!held.empty() && held.front()->samples.front().sampleIndex == 42
            && held.front()->samples.front().absoluteFrequencyHz == 2'920'000'000LL,
            "sampleIndex preserved and absolute frequency reconstructed");
    }
    // Retain every callback block. Azimuth changes force flushes without a large load;
    // the receiver must discard packets once all 32 preallocated buffers are retained.
    for (unsigned i = 0; i < 50; ++i) { h.sequence = 3+i; h.azimuthMilliDeg = i*1000; send(); std::this_thread::sleep_for(1ms); }
    check(waitFor([&] { return source.networkMetrics().poolExhaustions > 0; }), "retained buffers cause bounded drops");
    source.stop();
    {
        std::lock_guard lock(mutex);
        check(held.size() == 32 && held.front()->samples.front().amplitude == 80, "pool never grows or overwrites retained blocks");
    }
    const auto count = source.metrics().producedSamples;
    send(); std::this_thread::sleep_for(5ms);
    check(source.metrics().producedSamples == count, "stop is callback quiescence boundary");
}
void externalProcessStream(const char* executable)
{
    // Reserve an ephemeral port, then release it for the separate generator process.
    host::UdpSocket probe; host::Endpoint bind; host::ipv4Endpoint("127.0.0.1", 0, bind);
    std::string error;
    if (!probe.open(bind, error)) { check(false, error.c_str()); return; }
    const auto port = probe.localPort(); probe.close();
    QProcess process;
    process.start(QString::fromLocal8Bit(executable), {"--port", QString::number(port), "--duration", "10", "--rate", "100000"});
    if (!process.waitForStarted(3000) || !process.waitForReadyRead(3000)) {
        check(false, "standalone generator process starts"); process.kill(); process.waitForFinished(); return;
    }
    hardware::UdpBcoStreamSource source(endpoint(port)); source.configure(streamConfig());
    pipeline::DataIngestPipelineConfig config; config.blockPool = {16, 8192}; config.queueCapacity = 16; config.acceptingOnStart = true;
    pipeline::DataIngestPipeline pipeline(config);
    pipeline::BcoAcquisitionSession session(&source, &pipeline);
    session.openInput(); session.setAccepting(true);
    check(session.startSource().success, "receiver connects to separate process");
    check(waitFor([&] { return source.metrics().producedSamples >= 1000; }), "external process transmits samples");
    // Pulse settings cross the control channel. Disabled band leaves only heartbeats.
    source.setPulseBandConfigs({{0, false, 100.0, 10.0}});
    std::this_thread::sleep_for(250ms);
    const auto samplesBefore = source.metrics().producedSamples;
    const auto packetsBefore = source.networkMetrics().receivedDatagrams;
    std::this_thread::sleep_for(200ms);
    check(source.metrics().producedSamples == samplesBefore && source.networkMetrics().receivedDatagrams > packetsBefore,
          "disabled band yields heartbeats and no samples over network");
    source.setPulseBandConfigs({{0, true, 100.0, 10.0}});
    check(waitFor([&] { return source.metrics().producedSamples > samplesBefore; }), "new pulse revision resumes data");
    session.stopSource(); session.closeInput(true); pipeline.flushProcessing(2s);
    check(pipeline.metricsSnapshot().processedSamples == source.metrics().producedSamples
          && !session.metrics().droppedBlocks && !session.metrics().rejectedBlocks,
          "all delivered external samples processed");
    check(!source.metrics().malformedPackets, "external process format matches receiver");
    process.terminate();
    if (!process.waitForFinished(3000)) { process.kill(); process.waitForFinished(); check(false, "generator terminates gracefully"); }
    check(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, "external generator reports successful sends");
}
void generatorClockDrivesLiveViews()
{
    // The record budget is independent of the BCO clock. Test both sparse default
    // output and a budget exceeding the clock, using one visible beam at 2920 MHz.
    for (const auto samplePeriodNs : {320ULL, 1'000'000ULL}) {
        host::UdpGenerator generator;
        host::GeneratorOptions options;
        options.port = 0;
        if (samplePeriodNs == 1'000'000) options.samplesPerSecond = 1'000'000;
        std::string error;
        if (!generator.open(options, error)) { check(false, error.c_str()); continue; }
        std::atomic_bool stop{false};
        std::thread worker([&] { generator.run(stop); });
        hardware::UdpBcoStreamSource source(endpoint(generator.localPort()));
        auto stream = streamConfig();
        stream.timeBase.samplePeriodNs = samplePeriodNs;
        check(source.configure(stream).success, "configure clock regression receiver");
        if (samplePeriodNs == 320) source.setPulseBandConfigs({{0, true, 100000.0, 10000.0}});
        pipeline::DataIngestPipelineConfig config;
        config.blockPool = {16, 8192}; config.queueCapacity = 16; config.acceptingOnStart = true;
        config.waterfall.timeBase = stream.timeBase;
        config.spectrum.timeBase = stream.timeBase;
        config.processing.enableSignalParameterStage = false;
        pipeline::DataIngestPipeline pipeline(config);
        check(pipeline.start().success, "start clock regression pipeline");
        std::mutex mutex;
        std::optional<std::uint64_t> lastIndex;
        bool monotonic = true;
        std::atomic_bool ingestFailed{false};
        const auto started = std::chrono::steady_clock::now();
        check(source.start([&](auto block) {
            {
                std::lock_guard lock(mutex);
                for (const auto& sample : block->samples) {
                    if (lastIndex && sample.sampleIndex <= *lastIndex) monotonic = false;
                    lastIndex = sample.sampleIndex;
                }
            }
            pipeline::SignalBlockMetadata metadata;
            metadata.firstSampleIndex = block->stats.firstSampleIndex;
            metadata.lastSampleIndex = block->stats.lastSampleIndex;
            metadata.producedAt = block->stats.producedAt;
            if (!pipeline.ingestSamples(block->samples, metadata)) ingestFailed = true;
        }).success, "start clock regression receiver");
        check(waitFor([&] {
            const auto spectrum = pipeline.latestSpectrumSnapshot();
            return pipeline.waterfallRowQueueMetrics().pushedRows >= 3
                && spectrum && spectrum->sequenceId >= 3;
        }), "live waterfall and spectrum advance without stop/flush at default rate");
        const auto liveRows = pipeline.drainWaterfallRows(64);
        check(liveRows.size() >= 3, "several waterfall rows are available while recording");
        if (liveRows.size() >= 3) {
            check(liveRows.back().row.utcNs > liveRows.front().row.utcNs,
                  "live waterfall rows have advancing BCO timestamps");
        }
        source.stop();
        const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        {
            std::lock_guard lock(mutex);
            check(monotonic && lastIndex.has_value(), "scheduled windows never repeat or reverse indices");
            if (lastIndex) {
                const auto modelNs = (*lastIndex - stream.timeBase.firstSampleIndex) * samplePeriodNs;
                check(modelNs >= 40'000'000, "low record rate does not stall the BCO clock");
                check(modelNs <= static_cast<std::uint64_t>(elapsedNs) + 20'000'000,
                      "large record budget cannot advance BCO time ahead of wall time");
            }
        }
        check(!ingestFailed.load(), "scheduled stream fits the bounded pipeline");
        pipeline.stop();
        stop = true;
        worker.join();
    }
}

void expiredSubscriptionCannotReviveAfterStop()
{
    host::UdpGenerator generator; host::GeneratorOptions options; options.port = 0; options.lease = 200ms;
    std::string error;
    if (!generator.open(options, error)) { check(false, error.c_str()); return; }
    std::atomic_bool stop{false}; std::thread worker([&] { generator.run(stop); });
    host::UdpSocket client; host::Endpoint address; host::ipv4Endpoint("127.0.0.1", 0, address);
    if (!client.open(address, error)) { check(false, error.c_str()); stop = true; worker.join(); return; }
    address.port = generator.localPort();
    wire::Subscription subscription; subscription.header.stream = host::uniqueId(); subscription.header.count = 1;
    subscription.header.sequence = 1;
    std::array<std::byte, wire::maxDatagramBytes> buffer{}; host::Endpoint peer;
    auto encoded = wire::encodeSubscription(subscription, buffer);
    client.send(std::span(buffer).first(encoded.bytes), address);
    check(client.receive(buffer, peer, 1000) > 0, "initial lease produces DATA");
    std::this_thread::sleep_for(250ms);
    while (client.receive(buffer, peer, 0) > 0) {}
    subscription.header.sequence = 2;
    encoded = wire::encodeStop(subscription.header, buffer); client.send(std::span(buffer).first(encoded.bytes), address);
    subscription.header.sequence = 3;
    encoded = wire::encodeSubscription(subscription, buffer); client.send(std::span(buffer).first(encoded.bytes), address);
    check(client.receive(buffer, peer, 100) == 0, "delayed SUBSCRIBE cannot revive stopped expired lease");
    subscription.header.stream = host::uniqueId(); subscription.header.sequence = 1;
    encoded = wire::encodeSubscription(subscription, buffer); client.send(std::span(buffer).first(encoded.bytes), address);
    check(client.receive(buffer, peer, 1000) > 0, "new stream can subscribe after retired lease");
    stop = true; worker.join();
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    generatedStreamReachesPipeline(); malformedLossAndBoundedPool();
    expiredSubscriptionCannotReviveAfterStop();
    generatorClockDrivesLiveViews();
    if (argc > 1) externalProcessStream(argv[1]); else check(false, "missing standalone executable argument");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
