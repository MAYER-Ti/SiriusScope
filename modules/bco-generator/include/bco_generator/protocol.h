#pragma once
#include "bco_generator/generator.h"
#include <array>
#include <span>

namespace bco_generator::wire {
inline constexpr std::size_t headerBytes = 80;
inline constexpr std::size_t sampleBytes = 16;
inline constexpr std::size_t maxDatagramBytes = 1472;
inline constexpr std::size_t maxSamples = (maxDatagramBytes - headerBytes) / sampleBytes;
inline constexpr std::size_t maxBands = 8;
inline constexpr std::size_t bandBytes = 40;
enum class Kind : std::uint8_t { Data = 1, Subscribe = 2, Stop = 3 };
enum class Error { None, Size, Magic, Version, Header, Record, Capacity };
//! Host types only: encoding is explicit big endian, never memcpy(struct).
struct Header {
    Kind kind = Kind::Data;
    std::uint64_t stream = 0;
    std::uint64_t sequence = 0;
    std::uint64_t producer = 0;
    std::uint64_t firstSampleIndex = 0;
    std::uint64_t samplePeriodNs = 320;
    std::uint64_t startUtcNs = 0;
    std::uint32_t revision = 1;
    std::int32_t azimuthMilliDeg = -1;
    std::uint16_t count = 0;
};
struct BandSettings {
    std::uint8_t band = 0;
    bool enabled = true;
    bool pulsed = false;
    std::uint32_t widthHz = 500'000'000;
    std::uint64_t centerHz = 3'000'000'000;
    std::uint64_t pulsePeriodNs = 0;
    std::uint64_t pulseWidthNs = 0;
};
struct Subscription {
    Header header;
    std::array<BandSettings, maxBands> bands{};
};
struct Result {
    Error error = Error::None;
    std::size_t bytes = 0;
    explicit operator bool() const noexcept { return error == Error::None; }
};
//! Bounded, allocation-free codecs. A failed decode must not be consumed.
Result encodeData(Header header, std::span<const Sample> samples, std::span<std::byte> output) noexcept;
Result decodeData(std::span<const std::byte> input, Header& header, std::span<Sample> output) noexcept;
Result encodeSubscription(const Subscription& request, std::span<std::byte> output) noexcept;
Result decodeSubscription(std::span<const std::byte> input, Subscription& request) noexcept;
Result decodeHeader(std::span<const std::byte> input, Header& header) noexcept;
Result encodeStop(Header header, std::span<std::byte> output) noexcept;
//! Monotonic policy: late and duplicate packets are discarded, no reorder queue.
struct SequenceTracker {
    bool initialized = false;
    std::uint64_t next = 0;
    std::uint64_t missing = 0;
    std::uint64_t late = 0;
    bool accept(std::uint64_t sequence) noexcept;
};
} // namespace bco_generator::wire
