#include "hardware/data_source_factory.h"

#include "hardware/adapters/legacy_bco_sample_source_adapter.h"
#include "hardware/simulator/high_load_simulator_bco_stream_source.h"
#include "hardware/stubs/real_bco_stream_source_stub.h"

namespace siriusscope::hardware {

std::unique_ptr<IBcoStreamSource>
DataSourceFactory::createBcoStreamSourceFromLegacySimulator(
    const HardwareProfile& profile,
    IBcoSampleSource* legacySimulatorSource)
{
    if (!legacySimulatorSource || profile.dataSourceMode != DataSourceMode::Simulator) {
        return nullptr;
    }

    auto source = std::make_unique<LegacyBcoSampleSourceAdapter>(legacySimulatorSource);
    const auto configureResult = source->configure(profile.bcoStreamConfig);
    if (!configureResult) {
        return nullptr;
    }

    return source;
}

std::unique_ptr<IBcoStreamSource> DataSourceFactory::createRealBcoStreamSourceStub(
    const HardwareProfile& profile)
{
    if (profile.dataSourceMode != DataSourceMode::RealHardware) {
        return nullptr;
    }

    auto source = std::make_unique<RealBcoStreamSourceStub>();
    const auto configureResult = source->configure(profile.bcoStreamConfig);
    if (!configureResult) {
        return nullptr;
    }

    return source;
}

std::unique_ptr<IBcoStreamSource>
DataSourceFactory::createHighLoadSimulatorBcoStreamSource(
    const HardwareProfile& profile,
    infrastructure::IDiagnosticsSink* diagnosticsSink)
{
    if (profile.dataSourceMode != DataSourceMode::Simulator) {
        return nullptr;
    }

    auto source = std::make_unique<HighLoadSimulatorBcoStreamSource>(
        profile.simulatorLoadConfig,
        diagnosticsSink);
    const auto configureResult = source->configure(profile.bcoStreamConfig);
    if (!configureResult) {
        return nullptr;
    }

    return source;
}

} // namespace siriusscope::hardware
