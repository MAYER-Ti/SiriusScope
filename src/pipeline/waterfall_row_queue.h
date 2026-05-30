#pragma once

#include "pipeline/waterfall_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace siriusscope::pipeline {

enum class WaterfallOverflowPolicy
{
    DropOldest,
    DropNewest,
};

struct WaterfallRowQueueConfig
{
    std::size_t maxQueuedRows = 4096;
    WaterfallOverflowPolicy overflowPolicy = WaterfallOverflowPolicy::DropOldest;
};

struct WaterfallRowBatchMetadata
{
    std::uint64_t sourceSnapshotSequenceId = 0;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    double viewMinHz = 300'000'000.0;
    double viewMaxHz = 18'000'000'000.0;
    int renderBinCount = 1024;
    std::uint64_t rowPeriodNs = 20'000'000;
};

struct WaterfallQueuedRow
{
    std::uint64_t sequenceId = 0;
    std::uint64_t sourceSnapshotSequenceId = 0;
    std::int64_t sourceMinHz = 300'000'000;
    std::int64_t sourceMaxHz = 18'000'000'000LL;
    double viewMinHz = 300'000'000.0;
    double viewMaxHz = 18'000'000'000.0;
    int renderBinCount = 1024;
    std::uint64_t rowPeriodNs = 20'000'000;
    WaterfallSnapshotRow row;
};

struct WaterfallRowQueueMetrics
{
    std::size_t capacity = 0;
    std::size_t depth = 0;
    std::uint64_t pushedRows = 0;
    std::uint64_t drainedRows = 0;
    std::uint64_t droppedRows = 0;
    std::uint64_t latestRowSequenceId = 0;
};

struct WaterfallRowQueuePushResult
{
    std::uint64_t pushedRows = 0;
    std::uint64_t queuedRows = 0;
    std::uint64_t droppedRows = 0;
    std::size_t depth = 0;
    std::size_t capacity = 0;
    std::uint64_t latestRowSequenceId = 0;
};

class WaterfallRowQueue
{
public:
    explicit WaterfallRowQueue(WaterfallRowQueueConfig config = {});

    void reset();
    void setConfig(WaterfallRowQueueConfig config);

    WaterfallRowQueuePushResult pushRows(std::vector<WaterfallSnapshotRow> rows,
                                         WaterfallRowBatchMetadata metadata);
    std::vector<WaterfallQueuedRow> drain(std::size_t maxRows);
    WaterfallRowQueueMetrics metrics() const;

private:
    WaterfallQueuedRow makeQueuedRow(WaterfallSnapshotRow row,
                                     const WaterfallRowBatchMetadata& metadata);

    mutable std::mutex m_mutex;
    WaterfallRowQueueConfig m_config;
    std::deque<WaterfallQueuedRow> m_rows;
    WaterfallRowQueueMetrics m_metrics;
    std::uint64_t m_nextRowSequenceId = 1;
};

} // namespace siriusscope::pipeline
