#include "pipeline/waterfall_row_queue.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace siriusscope::pipeline {

WaterfallRowQueue::WaterfallRowQueue(WaterfallRowQueueConfig config)
    : m_config(config)
{
    m_metrics.capacity = m_config.maxQueuedRows;
}

void WaterfallRowQueue::reset()
{
    std::lock_guard lock(m_mutex);
    m_rows.clear();
    m_metrics = {};
    m_metrics.capacity = m_config.maxQueuedRows;
    m_nextRowSequenceId = 1;
    m_lastValidRowUtcMs.reset();
    m_hasWaterfallRowUtcDelta = false;
}

void WaterfallRowQueue::setConfig(WaterfallRowQueueConfig config)
{
    std::lock_guard lock(m_mutex);
    m_config = config;
    m_rows.clear();
    m_metrics = {};
    m_metrics.capacity = m_config.maxQueuedRows;
    m_nextRowSequenceId = 1;
    m_lastValidRowUtcMs.reset();
    m_hasWaterfallRowUtcDelta = false;
}

WaterfallRowQueuePushResult WaterfallRowQueue::pushRows(
    std::vector<WaterfallSnapshotRow> rows,
    WaterfallRowBatchMetadata metadata)
{
    std::lock_guard lock(m_mutex);

    WaterfallRowQueuePushResult result;
    result.capacity = m_config.maxQueuedRows;

    for (auto& row : rows) {
        ++m_metrics.pushedRows;
        ++result.pushedRows;

        if (m_config.maxQueuedRows == 0) {
            ++m_metrics.droppedRows;
            ++result.droppedRows;
            continue;
        }

        if (m_rows.size() >= m_config.maxQueuedRows) {
            if (m_config.overflowPolicy == WaterfallOverflowPolicy::DropOldest) {
                m_rows.pop_front();
                ++m_metrics.droppedRows;
                ++result.droppedRows;
            } else {
                ++m_metrics.droppedRows;
                ++result.droppedRows;
                continue;
            }
        }

        m_rows.push_back(makeQueuedRow(std::move(row), metadata));
        recordQueuedRowTimeMetrics(m_rows.back(), metadata, result);
        ++result.queuedRows;
    }

    m_metrics.depth = m_rows.size();
    result.depth = m_rows.size();
    result.latestRowSequenceId = m_metrics.latestRowSequenceId;
    result.waterfallRowUtcDeltaMinMs = m_metrics.waterfallRowUtcDeltaMinMs;
    result.waterfallRowUtcDeltaMaxMs = m_metrics.waterfallRowUtcDeltaMaxMs;
    result.waterfallExpectedRowPeriodMs = m_metrics.waterfallExpectedRowPeriodMs;
    return result;
}

std::vector<WaterfallQueuedRow> WaterfallRowQueue::drain(std::size_t maxRows)
{
    std::lock_guard lock(m_mutex);
    const auto count = std::min(maxRows, m_rows.size());
    std::vector<WaterfallQueuedRow> drained;
    drained.reserve(count);

    for (std::size_t index = 0; index < count; ++index) {
        drained.push_back(std::move(m_rows.front()));
        m_rows.pop_front();
    }

    m_metrics.drainedRows += static_cast<std::uint64_t>(drained.size());
    m_metrics.depth = m_rows.size();
    return drained;
}

WaterfallRowQueueMetrics WaterfallRowQueue::metrics() const
{
    std::lock_guard lock(m_mutex);
    auto metrics = m_metrics;
    metrics.capacity = m_config.maxQueuedRows;
    metrics.depth = m_rows.size();
    return metrics;
}

WaterfallQueuedRow WaterfallRowQueue::makeQueuedRow(
    WaterfallSnapshotRow row,
    const WaterfallRowBatchMetadata& metadata)
{
    WaterfallQueuedRow queued;
    queued.sequenceId = m_nextRowSequenceId++;
    queued.sourceSnapshotSequenceId = metadata.sourceSnapshotSequenceId;
    queued.sourceMinHz = metadata.sourceMinHz;
    queued.sourceMaxHz = metadata.sourceMaxHz;
    queued.viewMinHz = metadata.viewMinHz;
    queued.viewMaxHz = metadata.viewMaxHz;
    queued.renderBinCount = metadata.renderBinCount;
    queued.rowPeriodNs = metadata.rowPeriodNs;
    queued.row = std::move(row);
    m_metrics.latestRowSequenceId = queued.sequenceId;
    return queued;
}

void WaterfallRowQueue::recordQueuedRowTimeMetrics(
    const WaterfallQueuedRow& row,
    const WaterfallRowBatchMetadata& metadata,
    WaterfallRowQueuePushResult& result)
{
    const auto utcMs = row.row.utcNs / 1'000'000;
    if (utcMs <= 0) {
        return;
    }

    const double expectedMs =
        static_cast<double>(metadata.rowPeriodNs) / 1'000'000.0;
    m_metrics.waterfallExpectedRowPeriodMs = expectedMs;
    result.waterfallExpectedRowPeriodMs = expectedMs;

    if (!m_lastValidRowUtcMs) {
        m_lastValidRowUtcMs = utcMs;
        return;
    }

    const double deltaMs =
        static_cast<double>(utcMs - *m_lastValidRowUtcMs);
    m_lastValidRowUtcMs = utcMs;

    if (!m_hasWaterfallRowUtcDelta) {
        m_metrics.waterfallRowUtcDeltaMinMs = deltaMs;
        m_metrics.waterfallRowUtcDeltaMaxMs = deltaMs;
        m_hasWaterfallRowUtcDelta = true;
    } else {
        m_metrics.waterfallRowUtcDeltaMinMs =
            std::min(m_metrics.waterfallRowUtcDeltaMinMs, deltaMs);
        m_metrics.waterfallRowUtcDeltaMaxMs =
            std::max(m_metrics.waterfallRowUtcDeltaMaxMs, deltaMs);
    }

    const double thresholdMs = std::max(2.0, expectedMs * 0.25);
    if (expectedMs > 0.0 && std::abs(deltaMs - expectedMs) > thresholdMs) {
        ++m_metrics.waterfallTimebaseMismatchWarnings;
        ++result.waterfallTimebaseMismatchWarnings;
    }

    result.waterfallRowUtcDeltaMinMs = m_metrics.waterfallRowUtcDeltaMinMs;
    result.waterfallRowUtcDeltaMaxMs = m_metrics.waterfallRowUtcDeltaMaxMs;
}

} // namespace siriusscope::pipeline
