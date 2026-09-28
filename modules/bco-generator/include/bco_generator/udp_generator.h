#pragma once
#include "bco_generator/udp_socket.h"
#include <atomic>
#include <chrono>
#include <string>

namespace bco_generator::host {
struct GeneratorOptions {
    std::string bindHost = "127.0.0.1";
    std::uint16_t port = 46001;
    std::uint64_t samplesPerSecond = 1280;
    std::chrono::milliseconds lease{1000};
};
struct GeneratorMetrics {
    std::uint64_t datagrams = 0;
    std::uint64_t bytes = 0;
    std::uint64_t samples = 0;
    std::uint64_t sendErrors = 0;
    std::uint64_t rejectedRequests = 0;
};
//! One subscriber, paced 1/10 ms batches; no shared global generation state.
class UdpGenerator {
public:
    bool open(const GeneratorOptions& options, std::string& error);
    void run(const std::atomic_bool& stop, std::chrono::milliseconds duration = {});
    std::uint16_t localPort() const { return m_socket.localPort(); }
    GeneratorMetrics metrics() const { return m_metrics; } // after run joins
private:
    GeneratorOptions m_options;
    UdpSocket m_socket;
    GeneratorMetrics m_metrics;
};
}
