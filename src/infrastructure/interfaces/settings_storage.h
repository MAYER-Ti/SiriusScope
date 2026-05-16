#pragma once

#include "core/operation_result.h"

#include <cstddef>
#include <filesystem>

namespace siriusscope::infrastructure {

enum class ConnectionMode
{
    Simulator,
    Hardware
};

struct AppSettings
{
    ConnectionMode connectionMode = ConnectionMode::Simulator;
    std::filesystem::path dataDirectory = "data";
    std::size_t waterfallHistoryDepthRows = 360;
    std::size_t maxArchiveFileCount = 16;
};

class ISettingsStorage
{
public:
    virtual ~ISettingsStorage() = default;

    virtual AppSettings load() = 0;
    virtual core::OperationResult save(const AppSettings& settings) = 0;
};

} // namespace siriusscope::infrastructure
