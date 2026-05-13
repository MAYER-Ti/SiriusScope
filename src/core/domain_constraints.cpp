#include "core/domain_constraints.h"

namespace siriusscope::core {

RuntimeCapabilities defaultRuntimeCapabilities() noexcept
{
    return RuntimeCapabilities{
        DomainConstraints::currentBandCount,
        DomainConstraints::currentBeamCount,
        DomainConstraints::futureMaxBeamCount,
    };
}

} // namespace siriusscope::core
