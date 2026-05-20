#include "waterfallscanrecordingadapter.h"

#include "recordingcontroller.h"
#include "waterfallcontroller.h"

namespace siriusscope::app {

WaterfallScanRecordingAdapter::WaterfallScanRecordingAdapter(
    WaterfallController* waterfallController)
    : m_waterfallController(waterfallController)
{
}

WaterfallScanRecordingAdapter::WaterfallScanRecordingAdapter(
    RecordingController* recordingController)
    : m_recordingController(recordingController)
{
}

core::OperationResult WaterfallScanRecordingAdapter::beginScanRecording(
    std::uint64_t scanSessionId)
{
    if (!m_recordingController && !m_waterfallController) {
        return core::OperationResult::failure("recording control is not configured");
    }
    if (scanSessionId == 0) {
        return core::OperationResult::failure("scan session id is invalid");
    }
    if (m_activeScanSessionId) {
        return core::OperationResult::ok();
    }

    if (m_recordingController) {
        m_recordingController->startRecording();
    } else {
        m_waterfallController->startRecording();
    }
    m_activeScanSessionId = scanSessionId;
    return core::OperationResult::ok();
}

core::OperationResult WaterfallScanRecordingAdapter::endScanRecording(
    std::uint64_t scanSessionId)
{
    if (!m_recordingController && !m_waterfallController) {
        return core::OperationResult::failure("recording control is not configured");
    }
    if (!m_activeScanSessionId) {
        return core::OperationResult::ok();
    }
    if (scanSessionId != 0 && *m_activeScanSessionId != scanSessionId) {
        return core::OperationResult::ok();
    }

    if (m_recordingController) {
        m_recordingController->stopRecording();
    } else {
        m_waterfallController->stopRecording();
    }
    m_activeScanSessionId.reset();
    return core::OperationResult::ok();
}

} // namespace siriusscope::app
