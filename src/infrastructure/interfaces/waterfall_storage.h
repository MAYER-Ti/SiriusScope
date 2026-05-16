#pragma once

#include "core/operation_result.h"
#include "processing/sample_processor.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace siriusscope::infrastructure {

class IWaterfallStorage
{
public:
    virtual ~IWaterfallStorage() = default;

    virtual core::OperationResult append(const processing::WaterfallRow& row) = 0;
    virtual std::vector<processing::WaterfallRow> readRange(std::uint64_t sampleIndexStart,
                                                            std::uint64_t sampleIndexEnd,
                                                            std::size_t maxRows) = 0;
};

class NullWaterfallStorage final : public IWaterfallStorage
{
public:
    core::OperationResult append(const processing::WaterfallRow&) override
    {
        return core::OperationResult::ok();
    }

    std::vector<processing::WaterfallRow> readRange(std::uint64_t,
                                                    std::uint64_t,
                                                    std::size_t) override
    {
        return {};
    }
};

} // namespace siriusscope::infrastructure
