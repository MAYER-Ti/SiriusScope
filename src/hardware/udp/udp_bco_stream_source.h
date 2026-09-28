#pragma once
#include "hardware/interfaces/bco_stream_source.h"
#include "hardware/interfaces/antenna_azimuth_provider.h"
#include "hardware/simulator/simulator_pulse_config.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "bco_generator/udp_socket.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace siriusscope::hardware {
struct UdpBcoSourceConfig {
    std::string generatorHost = "127.0.0.1";
    std::uint16_t generatorPort = 46001;
    std::string bindHost = "0.0.0.0";
    std::uint16_t bindPort = 46000;
};
struct UdpReceiveMetrics {
    std::uint64_t receivedDatagrams = 0;
    std::uint64_t latePackets = 0;
    std::uint64_t foreignPackets = 0;
    std::uint64_t receiveErrors = 0;
    std::uint64_t controlSendErrors = 0;
    std::uint64_t poolExhaustions = 0;
};
//! IPv4 v1 receiver. Socket I/O and decoding run entirely on the RX worker.
class UdpBcoStreamSource final : public IBcoStreamSource {
public:
    explicit UdpBcoStreamSource(UdpBcoSourceConfig endpoint,
        infrastructure::IDiagnosticsSink* diagnostics = nullptr,
        IAntennaAzimuthProvider* antenna = nullptr);
    ~UdpBcoStreamSource() override;
    core::OperationResult configure(const BcoStreamConfig& config) override;
    core::OperationResult start(SampleBlockCallback callback) override;
    core::OperationResult stop() override;
    BcoSourceMetrics metrics() const override;
    UdpReceiveMetrics networkMetrics() const;
    void setPulseBandConfigs(std::vector<SimulatorPulseBandConfig> configs);
    std::uint16_t localPort() const { return m_socket.localPort(); } // after start, before stop
private:
    void receiveLoop(SampleBlockCallback callback);
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& text) const;
    UdpBcoSourceConfig m_endpoint;
    infrastructure::IDiagnosticsSink* m_diagnostics;
    IAntennaAzimuthProvider* m_antenna;
    bco_generator::host::UdpSocket m_socket;
    bco_generator::host::Endpoint m_peer;
    mutable std::mutex m_mutex;
    BcoStreamConfig m_config;
    std::vector<SimulatorPulseBandConfig> m_pulses;
    std::uint32_t m_revision = 1;
    BcoSourceMetrics m_metrics;
    UdpReceiveMetrics m_network;
    std::atomic_bool m_stop{false};
    bool m_configured = false;
    std::thread m_worker;
};
} // namespace siriusscope::hardware
