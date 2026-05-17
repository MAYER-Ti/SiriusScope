#pragma once

#include "infrastructure/interfaces/diagnostics_sink.h"

#include <QString>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace siriusscope::infrastructure {

class DiagnosticLogWriter final
{
public:
    struct Config
    {
        QString dataRootPath;
        int maxQueuedEvents = 1024;
    };

    explicit DiagnosticLogWriter(Config config);
    ~DiagnosticLogWriter();

    DiagnosticLogWriter(const DiagnosticLogWriter&) = delete;
    DiagnosticLogWriter& operator=(const DiagnosticLogWriter&) = delete;

    void append(const DiagnosticEvent& event);

private:
    void writerLoop();
    void writeEvent(const DiagnosticEvent& event);

    Config m_config;
    QString m_logsRootPath;
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<DiagnosticEvent> m_queue;
    bool m_stopRequested = false;
    std::size_t m_droppedEvents = 0;
};

} // namespace siriusscope::infrastructure
