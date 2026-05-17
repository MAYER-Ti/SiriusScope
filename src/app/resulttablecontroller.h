#pragma once

#include "app/interfaces/result_table_sink.h"
#include "app/resulttablemodel.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "infrastructure/interfaces/result_table_storage.h"

#include <QObject>
#include <QSet>
#include <QString>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace siriusscope::app {

class ResultTableController final
    : public QObject
    , public IResultTableSink
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    ResultTableController(ResultTableModel* model,
                          infrastructure::IResultTableStorage* storage,
                          infrastructure::IDiagnosticsSink* diagnosticsSink,
                          QObject* parent = nullptr);
    ~ResultTableController() override;

    ResultTableController(const ResultTableController&) = delete;
    ResultTableController& operator=(const ResultTableController&) = delete;

    bool loaded() const noexcept;
    QString lastError() const;

    Q_INVOKABLE void reload();
    bool waitUntilIdle(std::chrono::milliseconds timeout);

    core::OperationResult appendBearingResults(
        const ResultTableAppendContext& context,
        const std::vector<core::BearingResult>& results) override;

signals:
    void loadedChanged();
    void lastErrorChanged();
    void rowsAppended(int count);

private:
    using Task = std::function<void()>;

    bool enqueue(Task task);
    void workerLoop();
    void appendRowsOnWorker(std::vector<core::ResultTableRow> rows);
    void reloadOnWorker();

    void setLoaded(bool loaded);
    void setLastError(QString message);
    void setLastErrorAsync(QString message);
    void removePendingKeys(const std::vector<core::ResultTableRow>& rows);

    void publish(infrastructure::DiagnosticSeverity severity, const QString& message) const;
    void publishInfo(const QString& message) const;
    void publishWarning(const QString& message) const;
    void publishError(const QString& message) const;

    static QString rowKey(const core::ResultTableRow& row);

    ResultTableModel* m_model = nullptr;
    infrastructure::IResultTableStorage* m_storage = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;

    bool m_loaded = false;
    QString m_lastError;

    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::condition_variable m_idleCondition;
    std::deque<Task> m_tasks;
    std::size_t m_pendingTasks = 0;
    bool m_stopRequested = false;
    std::thread m_worker;

    std::mutex m_pendingKeysMutex;
    QSet<QString> m_pendingKeys;
};

} // namespace siriusscope::app
