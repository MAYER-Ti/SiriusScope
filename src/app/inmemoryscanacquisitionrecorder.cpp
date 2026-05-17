#include "inmemoryscanacquisitionrecorder.h"

#include <algorithm>
#include <utility>

namespace siriusscope::app {

core::OperationResult InMemoryScanAcquisitionRecorder::begin(
    const ScanAcquisitionMetadata& metadata)
{
    std::lock_guard lock(m_mutex);
    if (m_activeSession) {
        return core::OperationResult::failure("scan acquisition session is already active");
    }
    if (metadata.scanSessionId == 0) {
        return core::OperationResult::failure("scan acquisition session id is invalid");
    }
    if (sessionExists(metadata.scanSessionId)) {
        return core::OperationResult::failure("scan acquisition session id already exists");
    }

    m_activeSession = ScanAcquisitionSession{metadata, {}};
    return core::OperationResult::ok();
}

core::OperationResult InMemoryScanAcquisitionRecorder::append(
    const processing::BearingFrameObservation& observation)
{
    std::lock_guard lock(m_mutex);
    if (!m_activeSession) {
        return core::OperationResult::failure("scan acquisition session is not active");
    }

    m_activeSession->observations.push_back(observation);
    return core::OperationResult::ok();
}

core::OperationResult InMemoryScanAcquisitionRecorder::close(
    const ScanAcquisitionMetadata& finalMetadata)
{
    std::lock_guard lock(m_mutex);
    if (!m_activeSession) {
        return core::OperationResult::failure("scan acquisition session is not active");
    }
    if (m_activeSession->metadata.scanSessionId != finalMetadata.scanSessionId) {
        return core::OperationResult::failure("scan acquisition session id mismatch");
    }

    m_activeSession->metadata = finalMetadata;
    m_closedSessions.push_back(std::move(*m_activeSession));
    m_activeSession.reset();
    return core::OperationResult::ok();
}

std::vector<processing::BearingFrameObservation>
InMemoryScanAcquisitionRecorder::observations(std::uint64_t scanSessionId) const
{
    std::lock_guard lock(m_mutex);
    if (m_activeSession && m_activeSession->metadata.scanSessionId == scanSessionId) {
        return m_activeSession->observations;
    }

    const auto found =
        std::find_if(m_closedSessions.cbegin(),
                     m_closedSessions.cend(),
                     [scanSessionId](const ScanAcquisitionSession& session) {
                         return session.metadata.scanSessionId == scanSessionId;
                     });
    return found == m_closedSessions.cend()
        ? std::vector<processing::BearingFrameObservation>{}
        : found->observations;
}

bool InMemoryScanAcquisitionRecorder::active() const noexcept
{
    std::lock_guard lock(m_mutex);
    return m_activeSession.has_value();
}

bool InMemoryScanAcquisitionRecorder::sessionExists(std::uint64_t scanSessionId) const
{
    if (m_activeSession && m_activeSession->metadata.scanSessionId == scanSessionId) {
        return true;
    }

    return std::any_of(m_closedSessions.cbegin(),
                       m_closedSessions.cend(),
                       [scanSessionId](const ScanAcquisitionSession& session) {
                           return session.metadata.scanSessionId == scanSessionId;
                       });
}

} // namespace siriusscope::app
