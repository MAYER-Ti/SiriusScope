#include "pipeline/signal_block_pool.h"

#include <algorithm>
#include <utility>

namespace siriusscope::pipeline {

SignalBlockHandle::SignalBlockHandle(SignalBlockPool* pool, SignalBlock* block) noexcept
    : m_pool(pool)
    , m_block(block)
{
}

SignalBlockHandle::SignalBlockHandle(SignalBlockHandle&& other) noexcept
    : m_pool(std::exchange(other.m_pool, nullptr))
    , m_block(std::exchange(other.m_block, nullptr))
{
}

SignalBlockHandle& SignalBlockHandle::operator=(SignalBlockHandle&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();
    m_pool = std::exchange(other.m_pool, nullptr);
    m_block = std::exchange(other.m_block, nullptr);
    return *this;
}

SignalBlockHandle::~SignalBlockHandle()
{
    reset();
}

SignalBlock* SignalBlockHandle::releaseWithoutReturning() noexcept
{
    m_pool = nullptr;
    return std::exchange(m_block, nullptr);
}

void SignalBlockHandle::reset()
{
    if (m_pool && m_block) {
        m_pool->release(m_block);
    }
    m_pool = nullptr;
    m_block = nullptr;
}

SignalBlockPool::SignalBlockPool(SignalBlockPoolConfig config)
    : m_config(config)
{
    m_blocks.reserve(m_config.blockCount);
    m_available.reserve(m_config.blockCount);
    for (std::size_t i = 0; i < m_config.blockCount; ++i) {
        auto block = std::make_unique<SignalBlock>(m_config.maxSamplesPerBlock);
        m_available.push_back(block.get());
        m_blocks.push_back(std::move(block));
    }
}

SignalBlockPool::~SignalBlockPool() = default;

SignalBlockHandle SignalBlockPool::acquire()
{
    std::lock_guard lock(m_mutex);
    if (m_available.empty()) {
        ++m_exhausted;
        return {};
    }

    auto* block = m_available.back();
    m_available.pop_back();
    block->reset();
    ++m_acquired;
    return SignalBlockHandle(this, block);
}

SignalBlockPoolCounters SignalBlockPool::counters() const
{
    std::lock_guard lock(m_mutex);
    SignalBlockPoolCounters counters;
    counters.acquired = m_acquired;
    counters.released = m_released;
    counters.exhausted = m_exhausted;
    counters.capacity = m_blocks.size();
    counters.available = m_available.size();
    counters.inUse = counters.capacity - counters.available;
    counters.maxSamplesPerBlock = m_config.maxSamplesPerBlock;
    return counters;
}

void SignalBlockPool::release(SignalBlock* block)
{
    if (!block) {
        return;
    }

    std::lock_guard lock(m_mutex);
    block->reset();
    m_available.push_back(block);
    ++m_released;
}

} // namespace siriusscope::pipeline
