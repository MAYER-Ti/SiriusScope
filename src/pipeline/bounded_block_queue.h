#pragma once

#include "pipeline/signal_block_pool.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace siriusscope::pipeline {

struct BoundedBlockQueueMetrics
{
    std::size_t capacity = 0;
    std::size_t depth = 0;
    std::uint64_t pushedBlocks = 0;
    std::uint64_t poppedBlocks = 0;
    std::uint64_t droppedBlocks = 0;
    bool shutdown = false;
};

class BoundedBlockQueue
{
public:
    explicit BoundedBlockQueue(std::size_t capacity)
        : m_capacity(capacity)
    {
    }

    bool push(SignalBlockHandle block)
    {
        if (!block) {
            return false;
        }

        std::unique_lock lock(m_mutex);
        m_notFull.wait(lock, [this] {
            return m_shutdown || m_queue.size() < m_capacity;
        });

        if (m_shutdown || m_capacity == 0) {
            ++m_droppedBlocks;
            return false;
        }

        m_queue.push_back(std::move(block));
        ++m_pushedBlocks;
        lock.unlock();
        m_notEmpty.notify_one();
        return true;
    }

    bool tryPush(SignalBlockHandle block)
    {
        if (!block) {
            return false;
        }

        std::lock_guard lock(m_mutex);
        if (m_shutdown || m_capacity == 0 || m_queue.size() >= m_capacity) {
            ++m_droppedBlocks;
            return false;
        }

        m_queue.push_back(std::move(block));
        ++m_pushedBlocks;
        m_notEmpty.notify_one();
        return true;
    }

    std::optional<SignalBlockHandle> pop()
    {
        std::unique_lock lock(m_mutex);
        m_notEmpty.wait(lock, [this] {
            return m_shutdown || !m_queue.empty();
        });

        if (m_queue.empty()) {
            return std::nullopt;
        }

        auto block = std::move(m_queue.front());
        m_queue.pop_front();
        ++m_poppedBlocks;
        lock.unlock();
        m_notFull.notify_one();
        return block;
    }

    bool tryPop(SignalBlockHandle& out)
    {
        std::lock_guard lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }

        out = std::move(m_queue.front());
        m_queue.pop_front();
        ++m_poppedBlocks;
        m_notFull.notify_one();
        return true;
    }

    void shutdown()
    {
        {
            std::lock_guard lock(m_mutex);
            m_shutdown = true;
        }
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

    void reset()
    {
        std::lock_guard lock(m_mutex);
        m_queue.clear();
        m_pushedBlocks = 0;
        m_poppedBlocks = 0;
        m_droppedBlocks = 0;
        m_shutdown = false;
        m_notFull.notify_all();
    }

    void clear()
    {
        std::lock_guard lock(m_mutex);
        m_queue.clear();
        m_notFull.notify_all();
    }

    BoundedBlockQueueMetrics metrics() const
    {
        std::lock_guard lock(m_mutex);
        return BoundedBlockQueueMetrics{
            m_capacity,
            m_queue.size(),
            m_pushedBlocks,
            m_poppedBlocks,
            m_droppedBlocks,
            m_shutdown,
        };
    }

private:
    const std::size_t m_capacity = 0;
    mutable std::mutex m_mutex;
    std::condition_variable m_notEmpty;
    std::condition_variable m_notFull;
    std::deque<SignalBlockHandle> m_queue;
    std::uint64_t m_pushedBlocks = 0;
    std::uint64_t m_poppedBlocks = 0;
    std::uint64_t m_droppedBlocks = 0;
    bool m_shutdown = false;
};

} // namespace siriusscope::pipeline
