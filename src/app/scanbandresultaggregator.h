#pragma once

#include "core/domain_models.h"

#include <vector>

namespace siriusscope::app {

class ScanBandResultAggregator
{
public:
    static std::vector<core::BearingResult> aggregateByBand(
        const std::vector<core::BearingResult>& results);
};

} // namespace siriusscope::app
