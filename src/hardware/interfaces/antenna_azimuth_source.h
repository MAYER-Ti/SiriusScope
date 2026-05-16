#pragma once

#include "core/operation_result.h"

#include <chrono>
#include <functional>

namespace siriusscope::hardware {

struct AntennaAzimuthSample
{
    double degrees = 0.0;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::time_point{};
};

class IAntennaAzimuthSource
{
public:
    using AzimuthCallback = std::function<void(const AntennaAzimuthSample&)>;

    virtual ~IAntennaAzimuthSource() = default;

    virtual core::OperationResult start(AzimuthCallback callback) = 0;
    virtual core::OperationResult stop() = 0;
};

} // namespace siriusscope::hardware
