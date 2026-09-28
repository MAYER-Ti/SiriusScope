#include "bco_generator/protocol.h"
#include <cstdlib>
#include <iostream>

using namespace bco_generator;
namespace w = bco_generator::wire;
int failures = 0;
void check(bool ok, const char* text) { if (!ok) { ++failures; std::cerr << text << '\n'; } }
int main()
{
    std::array<std::byte, w::maxDatagramBytes> packet{};
    std::array<Sample, w::maxSamples> samples{}, decoded{};
    w::Header h; h.stream = 0x0102030405060708; h.producer = 7; h.firstSampleIndex = 42;
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = {42+i, 4, -123456, 0, 127, 1};
    auto result = w::encodeData(h, samples, packet);
    check(bool(result) && result.bytes == 1472 && result.bytes + 28 == 1500, "full packet exactly fits IPv4 MTU");
    check(packet[0] == std::byte{'S'} && packet[3] == std::byte{'O'}
        && packet[8] == std::byte{1} && packet[15] == std::byte{8}
        && packet[87] == std::byte{42} && packet[88] == std::byte{0xff}
        && packet[89] == std::byte{0xfe} && packet[90] == std::byte{0x1d}
        && packet[91] == std::byte{0xc0}, "golden network byte order including negative offset");
    w::Header read;
    check(bool(w::decodeData(packet, read, decoded)) && read.count == 87 && read.stream == h.stream
        && decoded[86].sampleIndex == 128 && decoded[0].frequencyOffsetHz == -123456
        && decoded[0].amplitude == 127 && decoded[0].bandIndex == 4, "full data round trip");
    check(w::decodeData(packet, read, std::span(decoded).first(86)).error == w::Error::Capacity, "bounded decode");
    for (const auto at : {0, 4, 6, 68, 72, 95}) {
        auto bad = packet; bad[at] ^= std::byte{0x80};
        check(!w::decodeData(bad, read, decoded), "reject magic/version/header/reserved mutation");
    }
    for (const auto at : {92, 93, 94}) {
        auto bad = packet; bad[at] = std::byte{128};
        check(!w::decodeData(bad, read, decoded), "reject invalid sample fields");
    }
    auto bad = packet; bad[94] = std::byte{0};
    check(!w::decodeData(bad, read, decoded), "amplitude zero is invalid");
    bad = packet; bad[103] = std::byte{1};
    check(!w::decodeData(bad, read, decoded), "sample index cannot precede origin/previous record");
    for (std::size_t size = 0; size < packet.size(); ++size)
        check(!w::decodeData(std::span(packet).first(size), read, decoded), "reject every truncated size");
    std::array<std::byte, 1473> oversized{};
    check(!w::decodeHeader(oversized, read), "reject oversized datagram");
    result = w::encodeData(h, std::span(samples).first(1), packet);
    check(result.bytes == 96 && bool(w::decodeData(std::span(packet).first(result.bytes), read, decoded)), "partial packet no padding");
    result = w::encodeData(h, {}, packet);
    check(result.bytes == 80 && bool(w::decodeData(std::span(packet).first(80), read, decoded)) && read.count == 0, "empty heartbeat");
    samples[0].frequencyOffsetHz = 250000001;
    check(!w::encodeData(h, std::span(samples).first(1), packet), "offset bound enforced");

    w::Subscription subscription; subscription.header = h; subscription.header.count = 2;
    subscription.bands[1].band = 1; subscription.bands[1].pulsed = true;
    subscription.bands[1].pulsePeriodNs = 10000; subscription.bands[1].pulseWidthNs = 500;
    result = w::encodeSubscription(subscription, packet);
    w::Subscription restored;
    check(result.bytes == 160 && bool(w::decodeSubscription(std::span(packet).first(160), restored))
        && restored.header.producer == 0 && restored.bands[1].pulseWidthNs == 500, "subscription round trip");
    bad = packet; bad[120] = std::byte{0};
    check(!w::decodeSubscription(std::span(bad).first(160), restored), "duplicate bands rejected");
    bad = packet; bad[81] = std::byte{2};
    check(!w::decodeSubscription(std::span(bad).first(160), restored), "boolean encoding strict");
    subscription.bands[1].pulseWidthNs = 10000;
    check(!w::encodeSubscription(subscription, packet), "invalid pulse timing");
    result = w::encodeStop(h, packet);
    check(result.bytes == 80 && bool(w::decodeHeader(std::span(packet).first(80), read))
        && read.kind == w::Kind::Stop && read.producer == 0, "stop control packet");
    w::SequenceTracker sequence;
    check(sequence.accept(2) && sequence.missing == 2 && sequence.accept(4) && sequence.missing == 3,
          "gaps including initial loss counted");
    check(!sequence.accept(4) && !sequence.accept(3) && sequence.late == 2 && sequence.accept(5),
          "duplicate and reordered packets rejected");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
