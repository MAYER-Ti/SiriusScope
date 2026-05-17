#pragma once

#include "app/interfaces/scan_acquisition_recorder.h"

#include <mutex>
#include <optional>
#include <vector>

namespace siriusscope::app {

class InMemoryScanAcquisitionRecorder final : public IScanAcquisitionRecorder
{
public:
    core::OperationResult begin(const ScanAcquisitionMetadata& metadata) override;
    core::OperationResult append(
        const processing::BearingFrameObservation& observation) override;
    core::OperationResult close(const ScanAcquisitionMetadata& finalMetadata) override;
    std::vector<processing::BearingFrameObservation> observations(
        std::uint64_t scanSessionId) const override;
    bool active() const noexcept override;

private:
    bool sessionExists(std::uint64_t scanSessionId) const;

    mutable std::mutex m_mutex;
    std::optional<ScanAcquisitionSession> m_activeSession;
    std::vector<ScanAcquisitionSession> m_closedSessions;
};

} // namespace siriusscope::app
