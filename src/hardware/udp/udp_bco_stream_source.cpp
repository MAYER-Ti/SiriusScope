#include "hardware/udp/udp_bco_stream_source.h"
#include "bco_generator/protocol.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <sstream>

namespace siriusscope::hardware {
namespace {
namespace wire = bco_generator::wire;
using Clock = std::chrono::steady_clock;
constexpr std::size_t blockCapacity = 8192;
constexpr std::size_t poolSize = 32;
}
UdpBcoStreamSource::UdpBcoStreamSource(UdpBcoSourceConfig endpoint,
    infrastructure::IDiagnosticsSink* diagnostics, IAntennaAzimuthProvider* antenna)
    : m_endpoint(std::move(endpoint)), m_diagnostics(diagnostics), m_antenna(antenna) {}
UdpBcoStreamSource::~UdpBcoStreamSource() { stop(); }
core::OperationResult UdpBcoStreamSource::configure(const BcoStreamConfig& config)
{
    std::lock_guard lock(m_mutex);
    if (m_worker.joinable()) return core::OperationResult::failure("UDP source is running");
    if (!config.timeBase.validate() || config.bandConfigs.empty() || config.bandConfigs.size() > wire::maxBands)
        return core::OperationResult::failure("invalid UDP source configuration");
    unsigned seen = 0; bool enabled = false;
    for (const auto& b : config.bandConfigs) {
        if (!b.validate() || (seen & (1U << b.bandIndex))) return core::OperationResult::failure("invalid/duplicate UDP band");
        seen |= 1U << b.bandIndex; enabled = enabled || b.enabled;
    }
    if (!enabled) return core::OperationResult::failure("no enabled UDP bands");
    m_config = config; m_configured = true; m_metrics = {}; m_network = {};
    return core::OperationResult::ok();
}
void UdpBcoStreamSource::setPulseBandConfigs(std::vector<SimulatorPulseBandConfig> configs)
{
    std::lock_guard lock(m_mutex);
    m_pulses = std::move(configs); ++m_revision;
    if (m_revision == 0) m_revision = 1;
}
core::OperationResult UdpBcoStreamSource::start(SampleBlockCallback callback)
{
    std::lock_guard lock(m_mutex);
    if (m_worker.joinable()) return core::OperationResult::ok();
    if (!m_configured || !callback) return core::OperationResult::failure("UDP source is not configured or callback is empty");
    bco_generator::host::Endpoint bind;
    if (!m_endpoint.generatorPort || !bco_generator::host::ipv4Endpoint(m_endpoint.generatorHost, m_endpoint.generatorPort, m_peer)
        || !bco_generator::host::ipv4Endpoint(m_endpoint.bindHost, m_endpoint.bindPort, bind))
        return core::OperationResult::failure("invalid UDP IPv4 endpoint");
    std::string error;
    if (!m_socket.open(bind, error)) return core::OperationResult::failure(error);
    m_stop = false; m_metrics = {}; m_network = {};
    try { m_worker = std::thread(&UdpBcoStreamSource::receiveLoop, this, std::move(callback)); }
    catch (const std::exception& e) { m_socket.close(); return core::OperationResult::failure(e.what()); }
    return core::OperationResult::ok();
}
core::OperationResult UdpBcoStreamSource::stop()
{
    m_stop = true;
    if (m_worker.joinable()) m_worker.join();
    m_socket.close();
    return core::OperationResult::ok();
}
BcoSourceMetrics UdpBcoStreamSource::metrics() const { std::lock_guard lock(m_mutex); return m_metrics; }
UdpReceiveMetrics UdpBcoStreamSource::networkMetrics() const { std::lock_guard lock(m_mutex); return m_network; }
void UdpBcoStreamSource::publish(infrastructure::DiagnosticSeverity severity, const std::string& text) const
{
    if (m_diagnostics) m_diagnostics->publish({severity, "UdpBcoStreamSource", text, std::chrono::system_clock::now()});
}
void UdpBcoStreamSource::receiveLoop(SampleBlockCallback callback)
{
    try {
        std::array<std::shared_ptr<BcoSampleBlock>, poolSize> pool;
        for (auto& slot : pool) { slot = std::make_shared<BcoSampleBlock>(); slot->samples.reserve(blockCapacity); }
        std::array<std::byte, wire::maxDatagramBytes> receiveBuffer{}, controlBuffer{};
        std::array<bco_generator::Sample, wire::maxSamples> decoded{};
        std::shared_ptr<BcoSampleBlock> block;
        BcoSourceMetrics metrics;
        UdpReceiveMetrics network;
        wire::SequenceTracker sequence;
        std::uint64_t reportedMissing = 0, reportedMalformed = 0;
        std::array<std::uint64_t, 7> warnedTotals{};
        wire::Subscription subscription;
        subscription.header.stream = bco_generator::host::uniqueId();
        subscription.header.firstSampleIndex = m_config.timeBase.firstSampleIndex;
        subscription.header.samplePeriodNs = m_config.timeBase.samplePeriodNs;
        subscription.header.startUtcNs = std::uint64_t(m_config.timeBase.recordingStartUtcNs);
        subscription.header.count = m_config.bandConfigs.size();
        std::uint64_t producer = 0;
        const auto started = Clock::now();
        auto nextControl = started, nextPublish = started + std::chrono::milliseconds{100};
        auto nextWarning = started + std::chrono::seconds{1}, lastData = started, batchStart = started;
        auto flush = [&] {
            if (!block) return;
            block->stats.lostPacketCount = sequence.missing - reportedMissing;
            block->stats.malformedPacketCount = metrics.malformedPackets - reportedMalformed;
            reportedMissing = sequence.missing; reportedMalformed = metrics.malformedPackets;
            block->stats.sampleCount = block->samples.size();
            if (!block->samples.empty()) {
                block->stats.firstSampleIndex = block->samples.front().sampleIndex;
                block->stats.lastSampleIndex = block->samples.back().sampleIndex;
            }
            ++metrics.producedBatches; metrics.producedSamples += block->samples.size();
            const auto before = Clock::now(); callback(block);
            metrics.maxCallbackDuration = std::max(metrics.maxCallbackDuration,
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - before));
            block.reset();
        };
        auto publishMetrics = [&] {
            metrics.lostPackets = sequence.missing;
            const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
            if (seconds > 0) {
                metrics.producedSamplesPerSecond = metrics.producedSamples / seconds;
                metrics.producedParsedSamplesPerSecond = metrics.producedSamplesPerSecond;
                metrics.producedRawBytesPerSecond = metrics.producedRawBytes / seconds;
                metrics.equivalentMegabytesPerSecond = metrics.producedSamplesPerSecond * sizeof(core::SignalSample) / 1e6;
            }
            network.latePackets = sequence.late;
            std::lock_guard lock(m_mutex); m_metrics = metrics; m_network = network;
        };
        bool receiving = false;
        while (!m_stop.load()) {
            auto now = Clock::now();
            if (now >= nextControl) {
                {
                    std::lock_guard lock(m_mutex);
                    subscription.header.revision = m_revision;
                    for (std::size_t i = 0; i < m_config.bandConfigs.size(); ++i) {
                        const auto& b = m_config.bandConfigs[i];
                        auto& w = subscription.bands[i]; w = {};
                        w.band = b.bandIndex; w.enabled = b.enabled; w.centerHz = b.centerFrequencyHz; w.widthHz = b.widthHz;
                        const auto pulse = std::find_if(m_pulses.begin(), m_pulses.end(), [&](const auto& p) { return p.bandIndex == b.bandIndex; });
                        if (pulse != m_pulses.end()) {
                            w.enabled = w.enabled && pulse->enabled;
                            if (pulse->enabled && std::isfinite(pulse->pulsePeriodUs) && std::isfinite(pulse->pulseWidthUs)
                                && pulse->pulseWidthUs > 0 && pulse->pulseWidthUs < pulse->pulsePeriodUs && pulse->pulsePeriodUs <= 60'000'000) {
                                w.pulsed = true; w.pulsePeriodNs = std::uint64_t(std::llround(pulse->pulsePeriodUs * 1000));
                                w.pulseWidthNs = std::uint64_t(std::llround(pulse->pulseWidthUs * 1000));
                            }
                        }
                    }
                }
                const double azimuth = m_antenna ? m_antenna->currentAzimuthDeg() : 0;
                subscription.header.azimuthMilliDeg = std::isfinite(azimuth) && azimuth >= 0 && azimuth < 360
                    ? std::int32_t(std::llround(azimuth * 1000)) % 360000 : -1;
                ++subscription.header.sequence;
                const auto encoded = wire::encodeSubscription(subscription, controlBuffer);
                if (!encoded || !m_socket.send(std::span<const std::byte>(controlBuffer).first(encoded.bytes), m_peer)) ++network.controlSendErrors;
                nextControl = now + std::chrono::milliseconds{100};
            }
            for (int packetIndex = 0; packetIndex < 256 && !m_stop.load(); ++packetIndex) {
                bco_generator::host::Endpoint peer;
                const int bytes = m_socket.receive(receiveBuffer, peer, packetIndex == 0 ? 2 : 0);
                if (bytes == 0) break;
                if (bytes < 0) { ++network.receiveErrors; continue; }
                ++network.receivedDatagrams;
                if (!(peer == m_peer)) { ++network.foreignPackets; continue; }
                wire::Header header;
                const auto packet = std::span<const std::byte>(receiveBuffer).first(bytes);
                if (!wire::decodeData(packet, header, decoded)) { ++metrics.malformedPackets; continue; }
                if (header.stream != subscription.header.stream || header.revision != subscription.header.revision
                    || header.firstSampleIndex != subscription.header.firstSampleIndex
                    || header.samplePeriodNs != subscription.header.samplePeriodNs || header.startUtcNs != subscription.header.startUtcNs
                    || (producer && header.producer != producer)) { ++network.foreignPackets; continue; }
                bool valid = true;
                for (std::size_t i = 0; i < header.count; ++i) {
                    auto& s = decoded[i];
                    const auto b = std::find_if(m_config.bandConfigs.begin(), m_config.bandConfigs.end(),
                        [&](const auto& band) { return band.bandIndex == s.bandIndex && band.enabled; });
                    if (b == m_config.bandConfigs.end() || s.beamIndex >= core::DomainConstraints::currentBeamCount
                        || s.frequencyOffsetHz < -b->widthHz / 2 || s.frequencyOffsetHz > b->widthHz / 2) { valid = false; break; }
                    s.absoluteFrequencyHz = b->centerFrequencyHz + s.frequencyOffsetHz;
                }
                if (!valid) { ++metrics.malformedPackets; continue; }
                if (!sequence.accept(header.sequence)) continue;
                producer = header.producer; lastData = Clock::now(); metrics.producedRawBytes += bytes;
                const auto azimuth = header.azimuthMilliDeg < 0 ? std::optional<double>{} : std::optional<double>{header.azimuthMilliDeg / 1000.0};
                if (block && (block->samples.size() + header.count > blockCapacity || block->stats.antennaAzimuthDeg != azimuth)) flush();
                if (!block) {
                    const auto available = std::find_if(pool.begin(), pool.end(), [](const auto& slot) { return slot.use_count() == 1; });
                    if (available == pool.end()) {
                        ++network.poolExhaustions; ++metrics.droppedBatches; metrics.droppedSamples += header.count; continue;
                    }
                    block = *available; block->samples.clear(); block->stats = {};
                    block->stats.producedAt = lastData; block->stats.antennaAzimuthDeg = azimuth; batchStart = lastData;
                }
                ++block->stats.packetCount;
                for (std::size_t i = 0; i < header.count; ++i) {
                    const auto& s = decoded[i];
                    block->samples.push_back({s.sampleIndex, s.bandIndex, s.frequencyOffsetHz,
                                             s.absoluteFrequencyHz, s.amplitude, s.beamIndex});
                }
            }
            now = Clock::now();
            if (block && now - batchStart >= std::chrono::milliseconds{10}) flush();
            if (now >= nextPublish) { publishMetrics(); nextPublish = now + std::chrono::milliseconds{100}; }
            if (now >= nextWarning) {
                const bool hasData = producer != 0 && now - lastData < std::chrono::seconds{1};
                if (hasData && !receiving) publish(infrastructure::DiagnosticSeverity::Info,
                    "UDP BCO: receiving data");
                receiving = hasData;
                if (now - lastData >= std::chrono::seconds{1}) publish(infrastructure::DiagnosticSeverity::Warning,
                    "UDP BCO: no matching data for 1 second; check generator/endpoint or restart recording after generator restart");
                const std::array totals{metrics.malformedPackets, sequence.missing, sequence.late,
                    network.poolExhaustions, network.controlSendErrors, network.receiveErrors, network.foreignPackets};
                if (totals != warnedTotals) {
                    std::ostringstream message;
                    message << "UDP totals: malformed=" << metrics.malformedPackets << " missing=" << sequence.missing
                            << " late=" << sequence.late << " poolExhaustions=" << network.poolExhaustions
                            << " controlSendErrors=" << network.controlSendErrors
                            << " receiveErrors=" << network.receiveErrors << " foreign=" << network.foreignPackets;
                    publish(infrastructure::DiagnosticSeverity::Warning, message.str());
                    warnedTotals = totals;
                }
                nextWarning = now + std::chrono::seconds{1};
            }
        }
        flush();
        ++subscription.header.sequence;
        const auto encoded = wire::encodeStop(subscription.header, controlBuffer);
        if (encoded) for (int i = 0; i < 3; ++i) m_socket.send(std::span<const std::byte>(controlBuffer).first(encoded.bytes), m_peer);
        publishMetrics();
    } catch (const std::exception& e) {
        publish(infrastructure::DiagnosticSeverity::Error, std::string("UDP receiver failed: ") + e.what());
        m_stop = true;
    }
}
} // namespace siriusscope::hardware
