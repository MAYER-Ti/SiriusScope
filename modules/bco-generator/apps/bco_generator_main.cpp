#include "bco_generator/udp_generator.h"
#include <charconv>
#include <csignal>
#include <iostream>
#include <string_view>
namespace {
std::atomic_bool stopped{false};
static_assert(std::atomic_bool::is_always_lock_free);
void stop(int) { stopped.store(true); }
bool number(std::string_view text, std::uint64_t& result) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
}
int main(int argc, char** argv)
{
    bco_generator::host::GeneratorOptions options;
    std::uint64_t durationSeconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option = argv[i];
        if (option == "--help") {
            std::cout << "bco-generator [--bind IPv4] [--port 46001] [--rate 1280] [--duration seconds]\n"
                         "One subscriber, UDP v1, IPv4 MTU 1500. Rate is sample slots/s.\n";
            return 0;
        }
        if (i + 1 == argc) { std::cerr << "missing option value\n"; return 2; }
        const std::string_view value = argv[++i];
        if (option == "--bind") { options.bindHost = value; continue; }
        std::uint64_t parsed = 0;
        if (!number(value, parsed)) { std::cerr << "invalid numeric option\n"; return 2; }
        if (option == "--port" && parsed > 0 && parsed <= 65535) options.port = std::uint16_t(parsed);
        else if (option == "--rate") options.samplesPerSecond = parsed;
        else if (option == "--duration" && parsed <= 86400) durationSeconds = parsed;
        else { std::cerr << "invalid option\n"; return 2; }
    }
    bco_generator::host::UdpGenerator generator;
    std::string error;
    if (!generator.open(options, error)) { std::cerr << error << '\n'; return 1; }
    std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
    std::cout << "UDP generator listening on " << options.bindHost << ':' << generator.localPort()
              << ", rate=" << options.samplesPerSecond << " slots/s\n" << std::flush;
    generator.run(stopped, std::chrono::milliseconds{durationSeconds * 1000});
    const auto metrics = generator.metrics();
    std::cout << "packets=" << metrics.datagrams << " bytes=" << metrics.bytes
              << " samples=" << metrics.samples << " sendErrors=" << metrics.sendErrors
              << " rejectedRequests=" << metrics.rejectedRequests << '\n';
    return metrics.sendErrors ? 1 : 0;
}
