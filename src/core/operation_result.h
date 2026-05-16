#pragma once

#include <string>
#include <utility>

namespace siriusscope::core {

struct OperationResult
{
    bool success = true;
    std::string message;

    static OperationResult ok()
    {
        return {};
    }

    static OperationResult failure(std::string message)
    {
        return {false, std::move(message)};
    }

    explicit operator bool() const noexcept
    {
        return success;
    }
};

} // namespace siriusscope::core
