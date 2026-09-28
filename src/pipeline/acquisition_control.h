#pragma once

#include "core/operation_result.h"
#include <chrono>

namespace siriusscope::pipeline {

//! Только control plane; сырые блоки не доступны пользователю интерфейса.
//! Команды вызываются последовательно одним владельцем сессии.
class IAcquisitionControl
{
public:
    virtual ~IAcquisitionControl() = default;
    virtual core::OperationResult startSource() = 0;
    virtual core::OperationResult stopSource() = 0;
    virtual bool sourceActive() const noexcept = 0;
    virtual core::OperationResult openInput() = 0;
    virtual void closeInput(bool drain) = 0;
    virtual void setAccepting(bool accepting) = 0;
    virtual void clearInput() = 0;
    virtual core::OperationResult flushInput(std::chrono::milliseconds timeout) = 0;
};

} // namespace siriusscope::pipeline
