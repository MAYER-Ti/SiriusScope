#include "resulttablecontroller.h"

#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace siriusscope::app {
namespace {

QString firstValidationMessage(const core::ValidationResult& validation)
{
    if (validation.issues().empty()) {
        return QStringLiteral("validation failed");
    }

    const auto& issue = validation.issues().front();
    if (!issue.message.empty()) {
        return QString::fromStdString(issue.message);
    }

    return QStringLiteral("validation code %1").arg(static_cast<int>(issue.code));
}

bool validPositive(double value)
{
    return std::isfinite(value) && value > 0.0;
}

} // namespace

ResultTableController::ResultTableController(
    ResultTableModel* model,
    infrastructure::IResultTableStorage* storage,
    infrastructure::IDiagnosticsSink* diagnosticsSink,
    QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_storage(storage)
    , m_diagnosticsSink(diagnosticsSink)
    , m_worker(&ResultTableController::workerLoop, this)
{
}

ResultTableController::~ResultTableController()
{
    {
        std::lock_guard lock(m_queueMutex);
        m_stopRequested = true;
    }
    m_queueCondition.notify_one();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool ResultTableController::loaded() const noexcept
{
    return m_loaded;
}

QString ResultTableController::lastError() const
{
    return m_lastError;
}

void ResultTableController::reload()
{
    setLoaded(false);
    if (!enqueue([this] {
            reloadOnWorker();
        })) {
        setLastError(QStringLiteral("result table worker is stopped"));
    }
}

bool ResultTableController::waitUntilIdle(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_queueMutex);
    return m_idleCondition.wait_for(lock, timeout, [this] {
        return m_pendingTasks == 0;
    });
}

core::OperationResult ResultTableController::appendBearingResults(
    const ResultTableAppendContext& context,
    const std::vector<core::BearingResult>& results)
{
    if (!m_model) {
        return core::OperationResult::failure("result table model is not configured");
    }
    if (!m_storage) {
        return core::OperationResult::failure("result table storage is not configured");
    }
    if (results.empty()) {
        publishInfo(QStringLiteral("No bearing results to append"));
        return core::OperationResult::ok();
    }

    std::vector<core::ResultTableRow> rows;
    rows.reserve(results.size());

    for (const auto& result : results) {
        const auto paramsIt =
            std::find_if(context.signalParameters.begin(),
                         context.signalParameters.end(),
                         [&result](const processing::SignalParameters& parameters) {
                             return parameters.bandIndex == result.bandIndex;
                         });
        std::optional<double> priUs;
        std::optional<double> pulseWidthUs;
        if (paramsIt != context.signalParameters.end()) {
            if (paramsIt->pulseRepetitionPeriodUs
                && validPositive(*paramsIt->pulseRepetitionPeriodUs)) {
                priUs = paramsIt->pulseRepetitionPeriodUs;
            }
            if (validPositive(paramsIt->pulseWidthUs)) {
                pulseWidthUs = paramsIt->pulseWidthUs;
            }
            if (priUs && pulseWidthUs && *pulseWidthUs >= *priUs) {
                priUs.reset();
                pulseWidthUs.reset();
                publishWarning(QStringLiteral("Ignored invalid signal parameters for band %1")
                                   .arg(result.bandIndex));
            }
        }

        auto created = core::ResultTableRow::fromBearingResult(
            result,
            context.antennaAzimuthDeg,
            priUs,
            pulseWidthUs,
            core::defaultRuntimeCapabilities());
        if (!created && (priUs || pulseWidthUs)) {
            publishWarning(QStringLiteral("Retrying result table row without signal parameters: %1")
                               .arg(firstValidationMessage(created.validation())));
            created = core::ResultTableRow::fromBearingResult(
                result,
                context.antennaAzimuthDeg,
                std::nullopt,
                std::nullopt,
                core::defaultRuntimeCapabilities());
        }
        if (!created) {
            publishWarning(QStringLiteral("Rejected invalid result table row: %1")
                               .arg(firstValidationMessage(created.validation())));
            continue;
        }
        rows.push_back(*created.value());
    }

    if (rows.empty()) {
        return core::OperationResult::failure("no valid result table rows");
    }

    std::vector<core::ResultTableRow> rowsToQueue;
    rowsToQueue.reserve(rows.size());
    {
        std::lock_guard lock(m_pendingKeysMutex);
        for (const auto& row : rows) {
            const auto key = rowKey(row);
            if (m_model->containsRow(row) || m_pendingKeys.contains(key)) {
                continue;
            }
            m_pendingKeys.insert(key);
            rowsToQueue.push_back(row);
        }
    }

    if (rowsToQueue.empty()) {
        publishInfo(QStringLiteral("All result table rows are duplicates"));
        return core::OperationResult::ok();
    }

    auto queuedRows = std::move(rowsToQueue);
    auto queuedRowsForCleanup = queuedRows;
    if (!enqueue([this, rows = std::move(queuedRows)]() mutable {
            appendRowsOnWorker(std::move(rows));
        })) {
        removePendingKeys(queuedRowsForCleanup);
        return core::OperationResult::failure("result table worker is stopped");
    }

    return core::OperationResult::ok();
}

bool ResultTableController::enqueue(Task task)
{
    {
        std::lock_guard lock(m_queueMutex);
        if (m_stopRequested) {
            return false;
        }
        m_tasks.push_back(std::move(task));
        ++m_pendingTasks;
    }
    m_queueCondition.notify_one();
    return true;
}

void ResultTableController::workerLoop()
{
    while (true) {
        Task task;
        {
            std::unique_lock lock(m_queueMutex);
            m_queueCondition.wait(lock, [this] {
                return m_stopRequested || !m_tasks.empty();
            });
            if (m_tasks.empty()) {
                if (m_stopRequested) {
                    break;
                }
                continue;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }

        task();

        {
            std::lock_guard lock(m_queueMutex);
            if (m_pendingTasks > 0) {
                --m_pendingTasks;
            }
            if (m_pendingTasks == 0) {
                m_idleCondition.notify_all();
            }
        }
    }
}

void ResultTableController::appendRowsOnWorker(std::vector<core::ResultTableRow> rows)
{
    std::vector<core::ResultTableRow> savedRows;
    savedRows.reserve(rows.size());

    for (const auto& row : rows) {
        const auto writeResult = m_storage->append(row);
        if (!writeResult) {
            const auto message = QStringLiteral("Result table append failed: %1")
                                     .arg(QString::fromStdString(writeResult.message));
            publishError(message);
            setLastErrorAsync(message);
            continue;
        }
        savedRows.push_back(row);
    }

    removePendingKeys(rows);

    if (savedRows.empty()) {
        return;
    }

    QMetaObject::invokeMethod(this,
                              [this, savedRows = std::move(savedRows)]() mutable {
                                  if (!m_model) {
                                      return;
                                  }
                                  const int appended = m_model->appendRows(savedRows);
                                  if (appended <= 0) {
                                      return;
                                  }
                                  publishInfo(QStringLiteral("Appended %1 result table rows")
                                                  .arg(appended));
                                  emit rowsAppended(appended);
                              },
                              Qt::QueuedConnection);
}

void ResultTableController::reloadOnWorker()
{
    if (!m_storage) {
        const auto message = QStringLiteral("result table storage is not configured");
        publishError(message);
        setLastErrorAsync(message);
        QMetaObject::invokeMethod(this,
                                  [this] {
                                      setLoaded(true);
                                  },
                                  Qt::QueuedConnection);
        return;
    }

    auto rows = m_storage->readAll();
    const int rowCount = static_cast<int>(rows.size());
    QMetaObject::invokeMethod(this,
                              [this, rows = std::move(rows), rowCount]() mutable {
                                  if (m_model) {
                                      m_model->resetRows(std::move(rows));
                                  }
                                  setLoaded(true);
                                  publishInfo(QStringLiteral("Loaded %1 result table rows")
                                                  .arg(rowCount));
                              },
                              Qt::QueuedConnection);
}

void ResultTableController::setLoaded(bool loaded)
{
    if (m_loaded == loaded) {
        return;
    }

    m_loaded = loaded;
    emit loadedChanged();
}

void ResultTableController::setLastError(QString message)
{
    if (m_lastError == message) {
        return;
    }

    m_lastError = std::move(message);
    emit lastErrorChanged();
}

void ResultTableController::setLastErrorAsync(QString message)
{
    QMetaObject::invokeMethod(this,
                              [this, message = std::move(message)]() mutable {
                                  setLastError(std::move(message));
                              },
                              Qt::QueuedConnection);
}

void ResultTableController::removePendingKeys(const std::vector<core::ResultTableRow>& rows)
{
    std::lock_guard lock(m_pendingKeysMutex);
    for (const auto& row : rows) {
        m_pendingKeys.remove(rowKey(row));
    }
}

void ResultTableController::publish(infrastructure::DiagnosticSeverity severity,
                                    const QString& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "ResultTable",
        message.toStdString(),
        std::chrono::system_clock::now(),
    });
}

void ResultTableController::publishInfo(const QString& message) const
{
    publish(infrastructure::DiagnosticSeverity::Info, message);
}

void ResultTableController::publishWarning(const QString& message) const
{
    publish(infrastructure::DiagnosticSeverity::Warning, message);
}

void ResultTableController::publishError(const QString& message) const
{
    publish(infrastructure::DiagnosticSeverity::Error, message);
}

QString ResultTableController::rowKey(const core::ResultTableRow& row)
{
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<qulonglong>(row.sampleIndex))
        .arg(static_cast<qlonglong>(row.resultTimeUtcNs))
        .arg(row.bandIndex);
}

} // namespace siriusscope::app
