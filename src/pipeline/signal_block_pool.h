#pragma once

#include "pipeline/signal_block.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace siriusscope::pipeline {

class SignalBlockPool;

struct SignalBlockPoolConfig
{
    std::size_t blockCount = 128;
    std::size_t maxSamplesPerBlock = 16'384;
};

struct SignalBlockPoolCounters
{
    std::uint64_t acquired = 0;
    std::uint64_t released = 0;
    std::uint64_t exhausted = 0;
    std::size_t capacity = 0;
    std::size_t available = 0;
    std::size_t inUse = 0;
    std::size_t maxSamplesPerBlock = 0;
};

class SignalBlockHandle
{
public:
    SignalBlockHandle() = default;
    SignalBlockHandle(const SignalBlockHandle&) = delete;
    SignalBlockHandle& operator=(const SignalBlockHandle&) = delete;

    SignalBlockHandle(SignalBlockHandle&& other) noexcept;
    SignalBlockHandle& operator=(SignalBlockHandle&& other) noexcept;
    ~SignalBlockHandle();

    explicit operator bool() const noexcept { return m_block != nullptr; }

    SignalBlock* get() noexcept { return m_block; }
    const SignalBlock* get() const noexcept { return m_block; }
    SignalBlock& operator*() noexcept { return *m_block; }
    const SignalBlock& operator*() const noexcept { return *m_block; }
    SignalBlock* operator->() noexcept { return m_block; }
    const SignalBlock* operator->() const noexcept { return m_block; }

    SignalBlock* releaseWithoutReturning() noexcept;
    void reset();

private:
    friend class SignalBlockPool;

    SignalBlockHandle(SignalBlockPool* pool, SignalBlock* block) noexcept;

    SignalBlockPool* m_pool = nullptr;
    SignalBlock* m_block = nullptr;
};

class SignalBlockPool
{
public:
    explicit SignalBlockPool(SignalBlockPoolConfig config = {});
    ~SignalBlockPool();

    SignalBlockPool(const SignalBlockPool&) = delete;
    SignalBlockPool& operator=(const SignalBlockPool&) = delete;

    SignalBlockHandle acquire();
    SignalBlockPoolCounters counters() const;

private:
    friend class SignalBlockHandle;

    void release(SignalBlock* block);

    SignalBlockPoolConfig m_config;
    mutable std::mutex m_mutex;
    std::vector<std::unique_ptr<SignalBlock>> m_blocks;
    std::vector<SignalBlock*> m_available;
    std::uint64_t m_acquired = 0;
    std::uint64_t m_released = 0;
    std::uint64_t m_exhausted = 0;
};

} // namespace siriusscope::pipeline
