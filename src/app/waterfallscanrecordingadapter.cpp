#include "waterfallscanrecordingadapter.h"

#include "waterfallcontroller.h"

namespace siriusscope::app {

WaterfallScanRecordingAdapter::WaterfallScanRecordingAdapter(
    WaterfallController* waterfallController)
    : m_waterfallController(waterfallController)
{
}

core::OperationResult WaterfallScanRecordingAdapter::beginScanRecording(
    std::uint64_t scanSessionId)
{
    if (!m_waterfallController) {
        return core::OperationResult::failure("waterfall controller is not configured");
    }
    if (scanSessionId == 0) {
        return core::OperationResult::failure("scan session id is invalid");
    }
    if (m_activeScanSessionId) {
        return core::OperationResult::ok();
    }

    m_waterfallController->startRecording();
    m_activeScanSessionId = scanSessionId;
    return core::OperationResult::ok();
}

core::OperationResult WaterfallScanRecordingAdapter::endScanRecording(
    std::uint64_t scanSessionId)
{
    if (!m_waterfallController) {
        return core::OperationResult::failure("waterfall controller is not configured");
    }
    if (!m_activeScanSessionId) {
        return core::OperationResult::ok();
    }
    if (scanSessionId != 0 && *m_activeScanSessionId != scanSessionId) {
        return core::OperationResult::ok();
    }

    m_waterfallController->stopRecording();
    m_activeScanSessionId.reset();
    return core::OperationResult::ok();
}

} // namespace siriusscope::app
