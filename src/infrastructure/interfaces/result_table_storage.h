#pragma once

#include "core/domain_models.h"
#include "core/operation_result.h"

#include <vector>

namespace siriusscope::infrastructure {

class IResultTableStorage
{
public:
    virtual ~IResultTableStorage() = default;

    virtual core::OperationResult append(const core::ResultTableRow& row) = 0;
    virtual std::vector<core::ResultTableRow> readAll() = 0;
};

} // namespace siriusscope::infrastructure
