#include "hardware/stubs/real_bco_stream_source_stub.h"

namespace siriusscope::hardware {

core::OperationResult RealBcoStreamSourceStub::configure(const BcoStreamConfig& config)
{
    std::lock_guard lock(m_mutex);
    m_config = config;
    return core::OperationResult::ok();
}

core::OperationResult RealBcoStreamSourceStub::start(SampleBlockCallback callback)
{
    (void)callback;
    return core::OperationResult::failure("real BCO stream source is not implemented");
}

core::OperationResult RealBcoStreamSourceStub::stop()
{
    return core::OperationResult::ok();
}

BcoSourceMetrics RealBcoStreamSourceStub::metrics() const
{
    return {};
}

} // namespace siriusscope::hardware
