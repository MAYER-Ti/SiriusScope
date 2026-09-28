#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace bco_generator::host {
//! Host-only IPv4 socket adapter. Embedded implementations can replace this target.
struct Endpoint {
    std::uint32_t address = 0; // network byte order
    std::uint16_t port = 0;    // host byte order
    bool operator==(const Endpoint&) const = default;
};
bool ipv4Endpoint(const std::string& host, std::uint16_t port, Endpoint& result);
std::uint64_t uniqueId();
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    bool open(Endpoint bindAddress, std::string& error);
    void close();
    bool send(std::span<const std::byte> data, Endpoint peer);
    // 0 = timeout/would-block, -1 = error or truncated datagram.
    int receive(std::span<std::byte> data, Endpoint& peer, int timeoutMs);
    std::uint16_t localPort() const;
private:
    std::intptr_t m_fd = -1;
};
} // namespace bco_generator::host
