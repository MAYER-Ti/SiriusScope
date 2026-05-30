#include "pipeline/waterfall_row_queue.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace siriusscope;

class TestRunner
{
public:
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            ++m_failed;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

pipeline::WaterfallSnapshotRow row(std::uint64_t id)
{
    pipeline::WaterfallSnapshotRow result;
    result.utcNs = static_cast<std::int64_t>(1'000'000'000 + id * 20'000'000);
    result.firstSampleIndex = id;
    result.lastSampleIndex = id;
    result.cells.resize(1);
    result.cells.front().beam0Peak = static_cast<std::uint16_t>(id);
    return result;
}

std::vector<pipeline::WaterfallSnapshotRow> rows(std::uint64_t first,
                                                 std::uint64_t last)
{
    std::vector<pipeline::WaterfallSnapshotRow> result;
    for (auto id = first; id <= last; ++id) {
        result.push_back(row(id));
    }
    return result;
}

pipeline::WaterfallRowBatchMetadata metadata(std::uint64_t sequenceId = 7)
{
    pipeline::WaterfallRowBatchMetadata result;
    result.sourceSnapshotSequenceId = sequenceId;
    result.sourceMinHz = 100;
    result.sourceMaxHz = 200;
    result.viewMinHz = 110.0;
    result.viewMaxHz = 190.0;
    result.renderBinCount = 1;
    result.rowPeriodNs = 20'000'000;
    return result;
}

void requireInvariant(TestRunner& test,
                      const pipeline::WaterfallRowQueueMetrics& metrics,
                      const std::string& label)
{
    test.require(metrics.pushedRows
                     == metrics.drainedRows
                         + static_cast<std::uint64_t>(metrics.depth)
                         + metrics.droppedRows,
                 label + ": pushed == drained + depth + dropped");
}

void testPushDrainReturnsRowsInOrder(TestRunner& test)
{
    pipeline::WaterfallRowQueue queue(
        pipeline::WaterfallRowQueueConfig{8, pipeline::WaterfallOverflowPolicy::DropOldest});

    const auto pushed = queue.pushRows(rows(1, 3), metadata());
    const auto drained = queue.drain(8);
    const auto metrics = queue.metrics();

    test.require(pushed.pushedRows == 3 && pushed.queuedRows == 3,
                 "queue accepts pushed rows");
    test.require(drained.size() == 3, "drain returns pushed rows");
    test.require(drained[0].row.firstSampleIndex == 1
                     && drained[1].row.firstSampleIndex == 2
                     && drained[2].row.firstSampleIndex == 3,
                 "drain preserves FIFO row order");
    test.require(drained[0].row.utcNs == row(1).utcNs,
                 "queued row preserves original utcNs");
    test.require(drained[0].rowPeriodNs == metadata().rowPeriodNs,
                 "queued row preserves row period metadata");
    test.require(drained[0].sourceMinHz == 100 && drained[0].sourceMaxHz == 200,
                 "queued row preserves source frequency metadata");
    test.require(drained[0].viewMinHz == 110.0 && drained[0].viewMaxHz == 190.0,
                 "queued row preserves view frequency metadata");
    requireInvariant(test, metrics, "after full drain");
}

void testDrainMaxRowsLeavesRemainder(TestRunner& test)
{
    pipeline::WaterfallRowQueue queue(
        pipeline::WaterfallRowQueueConfig{8, pipeline::WaterfallOverflowPolicy::DropOldest});

    queue.pushRows(rows(1, 5), metadata());
    const auto firstDrain = queue.drain(2);
    auto metrics = queue.metrics();
    test.require(firstDrain.size() == 2, "drain respects maxRows");
    test.require(metrics.depth == 3, "remaining rows stay queued");
    requireInvariant(test, metrics, "after partial drain");

    const auto secondDrain = queue.drain(8);
    metrics = queue.metrics();
    test.require(secondDrain.size() == 3, "second drain returns remaining rows");
    test.require(secondDrain.front().row.firstSampleIndex == 3
                     && secondDrain.back().row.firstSampleIndex == 5,
                 "remaining rows preserve order");
    requireInvariant(test, metrics, "after draining remainder");
}

void testDropOldestOverflowOrdering(TestRunner& test)
{
    pipeline::WaterfallRowQueue queue(
        pipeline::WaterfallRowQueueConfig{3, pipeline::WaterfallOverflowPolicy::DropOldest});

    queue.pushRows(rows(1, 5), metadata());
    const auto drained = queue.drain(10);
    const auto metrics = queue.metrics();

    test.require(drained.size() == 3, "DropOldest keeps capacity rows");
    test.require(drained[0].row.firstSampleIndex == 3
                     && drained[1].row.firstSampleIndex == 4
                     && drained[2].row.firstSampleIndex == 5,
                 "DropOldest capacity=3 push 1..5 drains 3,4,5");
    test.require(metrics.droppedRows == 2, "DropOldest counts dropped rows");
    requireInvariant(test, metrics, "after DropOldest overflow");
}

void testDropNewestOverflowOrdering(TestRunner& test)
{
    pipeline::WaterfallRowQueue queue(
        pipeline::WaterfallRowQueueConfig{3, pipeline::WaterfallOverflowPolicy::DropNewest});

    queue.pushRows(rows(1, 5), metadata());
    const auto drained = queue.drain(10);
    const auto metrics = queue.metrics();

    test.require(drained.size() == 3, "DropNewest keeps capacity rows");
    test.require(drained[0].row.firstSampleIndex == 1
                     && drained[1].row.firstSampleIndex == 2
                     && drained[2].row.firstSampleIndex == 3,
                 "DropNewest capacity=3 push 1..5 drains 1,2,3");
    test.require(metrics.droppedRows == 2, "DropNewest counts dropped rows");
    requireInvariant(test, metrics, "after DropNewest overflow");
}

void testResetClearsQueueAndMetrics(TestRunner& test)
{
    pipeline::WaterfallRowQueue queue(
        pipeline::WaterfallRowQueueConfig{3, pipeline::WaterfallOverflowPolicy::DropOldest});
    queue.pushRows(rows(1, 5), metadata());
    queue.drain(1);

    queue.reset();
    const auto metrics = queue.metrics();
    const auto drained = queue.drain(10);

    test.require(drained.empty(), "reset clears queued rows");
    test.require(metrics.capacity == 3 && metrics.depth == 0,
                 "reset keeps capacity and clears depth");
    test.require(metrics.pushedRows == 0 && metrics.drainedRows == 0
                     && metrics.droppedRows == 0 && metrics.latestRowSequenceId == 0,
                 "reset clears row queue metrics");
    requireInvariant(test, metrics, "after reset");
}

} // namespace

int main()
{
    TestRunner test;

    testPushDrainReturnsRowsInOrder(test);
    testDrainMaxRowsLeavesRemainder(test);
    testDropOldestOverflowOrdering(test);
    testDropNewestOverflowOrdering(test);
    testResetClearsQueueAndMetrics(test);

    return test.result();
}
