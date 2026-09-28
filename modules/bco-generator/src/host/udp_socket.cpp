#include "bco_generator/udp_socket.h"
#include <atomic>
#include <chrono>
#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace bco_generator::host {
namespace {
#ifdef _WIN32
using NativeSocket = SOCKET;
using SockLen = int;
bool initSockets() { static const bool ok = [] { WSADATA data; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }(); return ok; }
int socketError() { return WSAGetLastError(); }
bool wouldBlock(int code) { return code == WSAEWOULDBLOCK || code == WSAEINTR; }
#else
using NativeSocket = int;
using SockLen = socklen_t;
bool initSockets() { return true; }
int socketError() { return errno; }
bool wouldBlock(int code) { return code == EAGAIN || code == EWOULDBLOCK || code == EINTR; }
#endif
sockaddr_in address(Endpoint ep) {
    sockaddr_in result{}; result.sin_family = AF_INET; result.sin_addr.s_addr = ep.address;
    result.sin_port = htons(ep.port); return result;
}
}
bool ipv4Endpoint(const std::string& host, std::uint16_t port, Endpoint& result)
{
    if (!initSockets()) return false;
    in_addr addr{};
    if (inet_pton(AF_INET, host.c_str(), &addr) != 1) return false;
    result = {addr.s_addr, port}; return true;
}
std::uint64_t uniqueId()
{
    static std::atomic<std::uint64_t> last{0};
    auto value = std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    auto previous = last.load();
    while (!last.compare_exchange_weak(previous, value > previous ? value : previous + 1)) {}
    return value > previous ? value : previous + 1;
}
UdpSocket::~UdpSocket() { close(); }
void UdpSocket::close()
{
    if (m_fd == -1) return;
#ifdef _WIN32
    closesocket(NativeSocket(m_fd));
#else
    ::close(NativeSocket(m_fd));
#endif
    m_fd = -1;
}
bool UdpSocket::open(Endpoint endpoint, std::string& error)
{
    close();
    if (!initSockets()) { error = "socket initialization failed"; return false; }
    const auto fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) { error = "socket creation failed"; return false; }
#else
    if (fd < 0) { error = "socket creation failed"; return false; }
#endif
    m_fd = std::intptr_t(fd);
#ifndef _WIN32
    if (fd >= FD_SETSIZE) { error = "socket descriptor exceeds select capacity"; close(); return false; }
#endif
    const int buffer = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
#ifdef _WIN32
    BOOL df = TRUE;
    setsockopt(fd, IPPROTO_IP, IP_DONTFRAGMENT, reinterpret_cast<const char*>(&df), sizeof(df));
    u_long nonblocking = 1;
    if (ioctlsocket(fd, FIONBIO, &nonblocking) != 0) { error = "nonblocking socket setup failed"; close(); return false; }
#else
#ifdef IP_MTU_DISCOVER
    const int df = IP_PMTUDISC_DO;
    setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &df, sizeof(df));
#endif
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { error = "nonblocking socket setup failed"; close(); return false; }
#endif
    const auto bindAddress = address(endpoint);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&bindAddress), sizeof(bindAddress)) != 0) {
        error = "UDP bind failed (code " + std::to_string(socketError()) + ")"; close(); return false;
    }
    return true;
}
bool UdpSocket::send(std::span<const std::byte> data, Endpoint peer)
{
    const auto target = address(peer);
    const auto sent = ::sendto(NativeSocket(m_fd), reinterpret_cast<const char*>(data.data()),
                              int(data.size()), 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
    return sent == int(data.size());
}
int UdpSocket::receive(std::span<std::byte> data, Endpoint& peer, int timeoutMs)
{
    if (m_fd == -1) return -1;
    if (timeoutMs > 0) {
        fd_set readSet; FD_ZERO(&readSet); FD_SET(NativeSocket(m_fd), &readSet);
        timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        const auto ready = select(int(m_fd + 1), &readSet, nullptr, nullptr, &timeout);
        if (ready == 0) return 0;
        if (ready < 0) return wouldBlock(socketError()) ? 0 : -1;
    }
    sockaddr_in from{}; SockLen length = sizeof(from);
#ifdef _WIN32
    const int size = ::recvfrom(NativeSocket(m_fd), reinterpret_cast<char*>(data.data()), int(data.size()),
                                0, reinterpret_cast<sockaddr*>(&from), &length);
#else
    const auto size = ::recvfrom(NativeSocket(m_fd), data.data(), data.size(), MSG_TRUNC,
                                 reinterpret_cast<sockaddr*>(&from), &length);
#endif
    if (size < 0) return wouldBlock(socketError()) ? 0 : -1;
    if (std::size_t(size) > data.size()) return -1;
    peer = {from.sin_addr.s_addr, ntohs(from.sin_port)};
    // Empty datagrams are malformed protocol packets, not a timeout.
    return size == 0 ? -1 : int(size);
}
std::uint16_t UdpSocket::localPort() const
{
    sockaddr_in local{}; SockLen size = sizeof(local);
    if (getsockname(NativeSocket(m_fd), reinterpret_cast<sockaddr*>(&local), &size)) return 0;
    return ntohs(local.sin_port);
}
} // namespace bco_generator::host
