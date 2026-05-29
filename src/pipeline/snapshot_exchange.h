#pragma once

#include <memory>
#include <mutex>
#include <utility>

namespace siriusscope::pipeline {

template<typename Snapshot>
class SnapshotExchange
{
public:
    using SnapshotPtr = std::shared_ptr<const Snapshot>;

    void publish(SnapshotPtr snapshot)
    {
        std::lock_guard lock(m_mutex);
        m_latest = std::move(snapshot);
    }

    SnapshotPtr latest() const
    {
        std::lock_guard lock(m_mutex);
        return m_latest;
    }

    void reset()
    {
        std::lock_guard lock(m_mutex);
        m_latest.reset();
    }

private:
    mutable std::mutex m_mutex;
    SnapshotPtr m_latest;
};

} // namespace siriusscope::pipeline
