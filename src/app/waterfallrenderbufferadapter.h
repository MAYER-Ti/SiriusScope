#pragma once

#include "pipeline/waterfall_snapshot.h"
#include "pipeline/waterfall_row_queue.h"
#include "processing/sample_processor.h"
#include "waterfallstorage.h"

#include <QVector>

#include <cstdint>

namespace siriusscope::app {

struct WaterfallRenderBufferAdapterResult
{
    WaterfallRow row;
    bool hasVisibleCells = false;
};

class WaterfallRenderBufferAdapter
{
public:
    static WaterfallRenderBufferAdapterResult adaptFrame(
        const processing::WaterfallFrame& frame,
        qint64 utcMs,
        double sourceMinHz,
        double sourceMaxHz,
        int binCount);
    static WaterfallRenderBufferAdapterResult adaptSnapshotRow(
        const pipeline::WaterfallSnapshot& snapshot,
        const pipeline::WaterfallSnapshotRow& row,
        double viewMinHz,
        double viewMaxHz,
        int binCount);
    static WaterfallRenderBufferAdapterResult adaptQueuedRow(
        const pipeline::WaterfallQueuedRow& queuedRow,
        double viewMinHz,
        double viewMaxHz,
        int binCount);
};

} // namespace siriusscope::app
