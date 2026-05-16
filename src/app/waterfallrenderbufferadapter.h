#pragma once

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
};

} // namespace siriusscope::app
