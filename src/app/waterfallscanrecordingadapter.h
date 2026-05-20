#pragma once

#include "app/interfaces/scan_recording_control.h"

#include <optional>

namespace siriusscope::app {

class WaterfallController;
class RecordingController;

class WaterfallScanRecordingAdapter final : public IScanRecordingControl
{
public:
    explicit WaterfallScanRecordingAdapter(WaterfallController* waterfallController);
    explicit WaterfallScanRecordingAdapter(RecordingController* recordingController);

    core::OperationResult beginScanRecording(std::uint64_t scanSessionId) override;
    core::OperationResult endScanRecording(std::uint64_t scanSessionId) override;

private:
    WaterfallController* m_waterfallController = nullptr;
    RecordingController* m_recordingController = nullptr;
    std::optional<std::uint64_t> m_activeScanSessionId;
};

} // namespace siriusscope::app
