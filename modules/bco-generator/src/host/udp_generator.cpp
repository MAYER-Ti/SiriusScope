#include "bco_generator/udp_generator.h"
#include "bco_generator/protocol.h"
#include <algorithm>
#include <array>
#include <thread>
#include <limits>
#include <vector>

namespace bco_generator::host {
namespace {
using Clock = std::chrono::steady_clock;
// Shared physical scene; not adjusted to the receiver's selected frequencies.
std::uint64_t sampleIndexAt(std::uint64_t first, std::uint64_t elapsedNs,
                            std::uint64_t samplePeriodNs)
{
    const auto delta = elapsedNs / samplePeriodNs;
    return first + std::min(delta, std::numeric_limits<std::uint64_t>::max() - first);
}

constexpr std::array scene{
    RadioSource{45.0, 2'920'000'000LL, 112, 22.0, false, 0, 0},
    RadioSource{95.0, 5'825'000'000LL, 105, 22.0, false, 0, 0},
    RadioSource{135.0, 8'250'000'000LL, 116, 22.0, false, 0, 0},
    RadioSource{250.0, 9'670'000'000LL, 96, 24.0, true, 8'000'000LL, 80},
    RadioSource{310.0, 14'190'000'000LL, 82, 26.0, true, 12'000'000LL, 100}};
}
bool UdpGenerator::open(const GeneratorOptions& options, std::string& error)
{
    if (options.samplesPerSecond < 100 || options.samplesPerSecond > 10'000'000 || options.lease.count() < 200) {
        error = "rate must be 100..10000000 slots/s; lease >= 200 ms"; return false;
    }
    Endpoint bind;
    if (!ipv4Endpoint(options.bindHost, options.port, bind)) { error = "invalid IPv4 bind address"; return false; }
    m_options = options; m_metrics = {};
    return m_socket.open(bind, error);
}
void UdpGenerator::run(const std::atomic_bool& stop, std::chrono::milliseconds duration)
{
    std::array<std::byte, wire::maxDatagramBytes> buffer{};
    std::array<Sample, scene.size() * 2> scratch{};
    const auto period = std::chrono::milliseconds{m_options.samplesPerSecond >= 100'000 ? 1 : 10};
    std::vector<Sample> samples(std::size_t(m_options.samplesPerSecond * period.count() / 1000));
    std::array<Band, wire::maxBands> bands{};
    std::array<PulseConfig, wire::maxBands> pulses{};
    std::size_t bandCount = 0, pulseCount = 0;
    wire::Subscription current;
    Endpoint subscriber{};
    bool active = false;
    std::uint64_t sequence = 0;
    const auto producer = uniqueId();
    // Retired sessions cannot be revived by delayed Subscribe packets.
    std::array<std::uint64_t, 64> retired{}; std::size_t retiredIndex = 0;
    auto retire = [&] { if (current.header.stream) retired[retiredIndex++ % retired.size()] = current.header.stream; active = false; };
    const auto started = Clock::now();
    auto lastRequest = started, nextBatch = started, streamEpoch = started;
    const auto periodNs = std::chrono::duration_cast<std::chrono::nanoseconds>(period).count();
    while (!stop.load() && (duration.count() == 0 || Clock::now() - started < duration)) {
        // Bounded control work, followed by at most one generation batch.
        for (int n = 0; n < 16; ++n) {
            Endpoint peer;
            const auto size = m_socket.receive(buffer, peer, n == 0 ? 1 : 0);
            if (size == 0) break;
            if (size < 0) { ++m_metrics.rejectedRequests; continue; }
            wire::Header h;
            const auto packet = std::span<const std::byte>(buffer).first(size);
            if (!wire::decodeHeader(packet, h)) { ++m_metrics.rejectedRequests; continue; }
            if (active && (!(peer == subscriber) || (h.stream != current.header.stream))) {
                ++m_metrics.rejectedRequests; continue;
            }
            if (std::find(retired.begin(), retired.end(), h.stream) != retired.end()) continue;
            if (h.stream == current.header.stream && h.sequence <= current.header.sequence) continue;
            if (h.kind == wire::Kind::Stop) {
                if (peer == subscriber && h.stream == current.header.stream) retire();
                continue;
            }
            wire::Subscription request;
            if (!wire::decodeSubscription(packet, request)) { ++m_metrics.rejectedRequests; continue; }
            if (h.stream == current.header.stream && (!(peer == subscriber) || h.firstSampleIndex != current.header.firstSampleIndex
                           || h.samplePeriodNs != current.header.samplePeriodNs || h.startUtcNs != current.header.startUtcNs
                           || h.revision < current.header.revision)) { ++m_metrics.rejectedRequests; continue; }
            if (!active && h.stream != current.header.stream) {
                retire(); sequence = 0;
                streamEpoch = Clock::now(); nextBatch = streamEpoch;
            }
            current = request; subscriber = peer; active = true; lastRequest = Clock::now();
            bandCount = pulseCount = 0;
            for (std::size_t i = 0; i < h.count; ++i) {
                const auto& b = request.bands[i];
                if (!b.enabled) continue;
                const auto center = std::int64_t(b.centerHz);
                bands[bandCount++] = {b.band, center, center - b.widthHz / 2, center + b.widthHz / 2, true};
                if (b.pulsed) pulses[pulseCount++] = {b.band, true, double(b.pulsePeriodNs) / 1000, double(b.pulseWidthNs) / 1000};
            }
        }
        const auto now = Clock::now();
        if (active && now - lastRequest >= m_options.lease) active = false;
        if (!active || now < nextBatch) continue;
        // --rate bounds output work, not the BCO clock. Use scheduled time windows
        // even for sparse output, preserving pulse phase and skipping overdue work.
        // The epoch survives keepalives and lease renewal for the same stream.
        const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(now - streamEpoch).count();
        const auto batchStartNs = static_cast<std::uint64_t>((elapsedNs / periodNs) * periodNs);
        const auto batchEndNs = batchStartNs + static_cast<std::uint64_t>(periodNs);
        const auto firstIndex = sampleIndexAt(current.header.firstSampleIndex, batchStartNs, current.header.samplePeriodNs);
        const auto endIndex = sampleIndexAt(current.header.firstSampleIndex, batchEndNs, current.header.samplePeriodNs);
        // Core output capacity also bounds temporal slots. Clamp it to this window
        // so large rate budgets cannot overlap the next window or speed up time.
        const auto capacity = static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(), endIndex - firstIndex));
        BatchRequest request{std::span(bands).first(bandCount), std::span(pulses).first(pulseCount), scene,
            firstIndex, current.header.firstSampleIndex, current.header.samplePeriodNs,
            current.header.azimuthMilliDeg < 0 ? 0.0 : current.header.azimuthMilliDeg / 1000.0, 0, pulseCount == 0};
        const auto generated = generate(request, std::span(samples).first(capacity), scratch);
        // Each datagram is independently decodable; no cross-packet sample fragments.
        std::size_t offset = 0;
        do {
            auto header = current.header; header.producer = producer; header.sequence = sequence++;
            const auto count = std::min(wire::maxSamples, generated.sampleCount - offset);
            const auto encoded = wire::encodeData(header, std::span(samples).subspan(offset, count), buffer);
            if (!encoded || !m_socket.send(std::span<const std::byte>(buffer).first(encoded.bytes), subscriber)) {
                ++m_metrics.sendErrors;
            } else { ++m_metrics.datagrams; m_metrics.bytes += encoded.bytes; m_metrics.samples += count; }
            offset += count;
        } while (offset < generated.sampleCount && !stop.load());
        nextBatch = streamEpoch + std::chrono::nanoseconds{batchEndNs};
    }
}
} // namespace bco_generator::host
