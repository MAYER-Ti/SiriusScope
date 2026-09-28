#include "bco_generator/protocol.h"
#include <algorithm>
#include <bit>
#include <limits>

namespace bco_generator::wire {
namespace {
void put(std::span<std::byte> out, std::size_t at, std::uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i) out[at + bytes - 1 - i] = std::byte((value >> (i * 8)) & 255);
}
std::uint64_t get(std::span<const std::byte> in, std::size_t at, std::size_t bytes)
{
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < bytes; ++i) result = (result << 8) | std::to_integer<unsigned>(in[at + i]);
    return result;
}
bool valid(const Header& h)
{
    return h.stream != 0 && h.samplePeriodNs > 0 && h.revision != 0
        && h.startUtcNs <= std::uint64_t(std::numeric_limits<std::int64_t>::max())
        && h.azimuthMilliDeg >= -1 && h.azimuthMilliDeg < 360000
        && h.sequence != std::numeric_limits<std::uint64_t>::max();
}
Result writeHeader(Header h, std::span<std::byte> out, std::size_t recordSize)
{
    const auto size = headerBytes + h.count * recordSize;
    if (out.size() < size) return {Error::Capacity};
    if (!valid(h)) return {Error::Header};
    std::fill(out.begin(), out.begin() + size, std::byte{0});
    put(out, 0, 0x5342434f, 4); // SBCO
    put(out, 4, 1, 1); put(out, 5, static_cast<unsigned>(h.kind), 1);
    put(out, 6, headerBytes, 2);
    put(out, 8, h.stream, 8); put(out, 16, h.sequence, 8); put(out, 24, h.producer, 8);
    put(out, 32, h.firstSampleIndex, 8); put(out, 40, h.samplePeriodNs, 8);
    put(out, 48, h.startUtcNs, 8); put(out, 56, h.revision, 4);
    put(out, 60, std::bit_cast<std::uint32_t>(h.azimuthMilliDeg), 4);
    put(out, 64, h.count, 2); put(out, 66, recordSize, 2);
    return {Error::None, size};
}
bool validBand(const BandSettings& b)
{
    return b.band < maxBands && b.widthHz > 0 && b.widthHz <= 500'000'000
        && b.centerHz >= 300'000'000 + b.widthHz / 2
        && b.centerHz <= 18'000'000'000ULL - b.widthHz / 2
        && (!b.pulsed || (b.pulseWidthNs > 0 && b.pulsePeriodNs > b.pulseWidthNs
                         && b.pulsePeriodNs <= 60'000'000'000ULL));
}
}
Result decodeHeader(std::span<const std::byte> in, Header& h) noexcept
{
    if (in.size() < headerBytes || in.size() > maxDatagramBytes) return {Error::Size};
    if (get(in, 0, 4) != 0x5342434f) return {Error::Magic};
    if (get(in, 4, 1) != 1) return {Error::Version};
    if (get(in, 6, 2) != headerBytes || get(in, 68, 4) || get(in, 72, 8)) return {Error::Header};
    const auto kind = get(in, 5, 1);
    if (kind < 1 || kind > 3) return {Error::Header};
    h.kind = static_cast<Kind>(kind);
    h.stream = get(in, 8, 8); h.sequence = get(in, 16, 8); h.producer = get(in, 24, 8);
    h.firstSampleIndex = get(in, 32, 8); h.samplePeriodNs = get(in, 40, 8);
    h.startUtcNs = get(in, 48, 8); h.revision = get(in, 56, 4);
    h.azimuthMilliDeg = std::bit_cast<std::int32_t>(std::uint32_t(get(in, 60, 4)));
    h.count = get(in, 64, 2);
    const std::size_t record = h.kind == Kind::Data ? sampleBytes : h.kind == Kind::Subscribe ? bandBytes : 0;
    const std::size_t maximum = h.kind == Kind::Data ? maxSamples : h.kind == Kind::Subscribe ? maxBands : 0;
    if (!valid(h) || h.count > maximum || (h.kind == Kind::Subscribe && h.count == 0)
        || get(in, 66, 2) != record || in.size() != headerBytes + record * h.count
        || (h.kind == Kind::Data && h.producer == 0)
        || (h.kind != Kind::Data && h.producer != 0)) return {Error::Header};
    return {Error::None, in.size()};
}
Result encodeData(Header h, std::span<const Sample> samples, std::span<std::byte> out) noexcept
{
    if (samples.size() > maxSamples) return {Error::Size};
    if (h.producer == 0) return {Error::Header};
    h.kind = Kind::Data; h.count = samples.size();
    auto result = writeHeader(h, out, sampleBytes);
    if (!result) return result;
    std::uint64_t previous = h.firstSampleIndex;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        if (s.sampleIndex < previous || s.bandIndex < 0 || s.bandIndex >= 8
            || s.beamIndex < 0 || s.beamIndex >= 8 || s.amplitude < 1 || s.amplitude > 127
            || s.frequencyOffsetHz < -250'000'000 || s.frequencyOffsetHz > 250'000'000) return {Error::Record};
        previous = s.sampleIndex;
        const auto at = headerBytes + i * sampleBytes;
        put(out, at, s.sampleIndex, 8);
        put(out, at + 8, std::bit_cast<std::uint32_t>(std::int32_t(s.frequencyOffsetHz)), 4);
        put(out, at + 12, s.bandIndex, 1); put(out, at + 13, s.beamIndex, 1); put(out, at + 14, s.amplitude, 1);
    }
    return result;
}
Result decodeData(std::span<const std::byte> in, Header& h, std::span<Sample> out) noexcept
{
    auto result = decodeHeader(in, h);
    if (!result) return result;
    if (h.kind != Kind::Data) return {Error::Header};
    if (out.size() < h.count) return {Error::Capacity};
    auto previous = h.firstSampleIndex;
    for (std::size_t i = 0; i < h.count; ++i) {
        const auto at = headerBytes + i * sampleBytes;
        Sample s;
        s.sampleIndex = get(in, at, 8);
        s.frequencyOffsetHz = std::bit_cast<std::int32_t>(std::uint32_t(get(in, at + 8, 4)));
        s.bandIndex = get(in, at + 12, 1); s.beamIndex = get(in, at + 13, 1); s.amplitude = get(in, at + 14, 1);
        if (s.sampleIndex < previous || s.bandIndex >= 8 || s.beamIndex >= 8 || s.amplitude < 1 || s.amplitude > 127
            || s.frequencyOffsetHz < -250'000'000 || s.frequencyOffsetHz > 250'000'000
            || get(in, at + 15, 1)) return {Error::Record};
        previous = s.sampleIndex;
        out[i] = s; // absoluteFrequencyHz must be derived from the agreed band config.
    }
    return result;
}
Result encodeSubscription(const Subscription& r, std::span<std::byte> out) noexcept
{
    if (r.header.count == 0 || r.header.count > maxBands) return {Error::Size};
    Header h = r.header; h.kind = Kind::Subscribe; h.producer = 0;
    auto result = writeHeader(h, out, bandBytes);
    if (!result) return result;
    unsigned seen = 0;
    for (std::size_t i = 0; i < h.count; ++i) {
        const auto& b = r.bands[i];
        if (!validBand(b) || (seen & (1U << b.band))) return {Error::Record};
        seen |= 1U << b.band;
        const auto at = headerBytes + i * bandBytes;
        put(out, at, b.band, 1); put(out, at + 1, b.enabled, 1); put(out, at + 2, b.pulsed, 1);
        put(out, at + 4, b.widthHz, 4); put(out, at + 8, b.centerHz, 8);
        put(out, at + 16, b.pulsePeriodNs, 8); put(out, at + 24, b.pulseWidthNs, 8);
    }
    return result;
}
Result decodeSubscription(std::span<const std::byte> in, Subscription& r) noexcept
{
    auto result = decodeHeader(in, r.header);
    if (!result) return result;
    if (r.header.kind != Kind::Subscribe) return {Error::Header};
    unsigned seen = 0;
    for (std::size_t i = 0; i < r.header.count; ++i) {
        const auto at = headerBytes + i * bandBytes;
        auto& b = r.bands[i];
        b.band = get(in, at, 1); b.enabled = get(in, at + 1, 1) != 0; b.pulsed = get(in, at + 2, 1) != 0;
        b.widthHz = get(in, at + 4, 4); b.centerHz = get(in, at + 8, 8);
        b.pulsePeriodNs = get(in, at + 16, 8); b.pulseWidthNs = get(in, at + 24, 8);
        if (!validBand(b) || (seen & (1U << b.band)) || get(in, at + 1, 1) > 1
            || get(in, at + 2, 1) > 1 || get(in, at + 3, 1) || get(in, at + 32, 8)) return {Error::Record};
        seen |= 1U << b.band;
    }
    return result;
}
Result encodeStop(Header h, std::span<std::byte> out) noexcept
{
    h.kind = Kind::Stop; h.count = 0; h.producer = 0;
    return writeHeader(h, out, 0);
}
bool SequenceTracker::accept(std::uint64_t sequence) noexcept
{
    if (sequence == std::numeric_limits<std::uint64_t>::max()) return false;
    if (initialized && sequence < next) { ++late; return false; }
    if (sequence > next) missing += sequence - next;
    initialized = true; next = sequence + 1;
    return true;
}
} // namespace bco_generator::wire
