#include "hardware/udp/udp_bco_control.h"
#include <algorithm>
namespace siriusscope::hardware {
core::OperationResult UdpBcoControl::applyBandConfig(const core::BandConfig& config)
{
    if (!config.validate()) return core::OperationResult::failure("invalid UDP band configuration");
    auto found = std::find_if(m_bands.begin(), m_bands.end(), [&](const auto& b) { return b.bandIndex == config.bandIndex; });
    if (found == m_bands.end()) m_bands.push_back(config); else *found = config;
    return core::OperationResult::ok();
}
core::OperationResult UdpBcoControl::applyBandConfigs(const std::vector<core::BandConfig>& configs)
{
    if (configs.empty() || configs.size() > 8) return core::OperationResult::failure("invalid UDP band count");
    unsigned seen = 0;
    for (const auto& b : configs) {
        if (!b.validate() || (seen & (1U << b.bandIndex))) return core::OperationResult::failure("invalid/duplicate UDP band");
        seen |= 1U << b.bandIndex;
    }
    m_bands = configs; return core::OperationResult::ok();
}
core::OperationResult UdpBcoControl::startProcessing(const BcoProcessingStartCommand& command)
{
    if (!m_source) return core::OperationResult::failure("UDP source is missing");
    const auto validated = applyBandConfigs(command.bandConfigs);
    if (!validated) return validated;
    return m_source->configure({command.bandConfigs, command.timeBase, command.sessionId});
}
}
