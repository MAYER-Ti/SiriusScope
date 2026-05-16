#pragma once

#include "app/waterfallstorage.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "infrastructure/storage/waterfall_storage_format.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QString>
#include <QVector>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace siriusscope::infrastructure {

class BinaryWaterfallSessionStorage final : public IWaterfallSessionStorage
{
public:
    struct Config
    {
        QString dataRootPath;
        int maxSessions = 20;
        bool fsyncMetadata = false;
    };

    explicit BinaryWaterfallSessionStorage(Config config,
                                           IDiagnosticsSink* diagnosticsSink = nullptr);
    ~BinaryWaterfallSessionStorage() override;

    BinaryWaterfallSessionStorage(const BinaryWaterfallSessionStorage&) = delete;
    BinaryWaterfallSessionStorage& operator=(const BinaryWaterfallSessionStorage&) = delete;

    QVector<WaterfallSessionMetadata> listSessions() const override;
    std::optional<WaterfallSessionMetadata> session(const WaterfallSessionId& id) const override;
    std::optional<WaterfallSessionMetadata> latestSession() const override;
    std::optional<WaterfallSessionMetadata> previousSession(
        const WaterfallSessionId& id) const override;
    std::optional<WaterfallSessionMetadata> nextSession(
        const WaterfallSessionId& id) const override;

    WaterfallSessionMetadata startSession(WaterfallSessionMetadata metadata) override;
    bool closeSession(const WaterfallSessionId& id, qint64 endUtcMs) override;
    void appendRow(const WaterfallSessionId& id, const WaterfallRow& row) override;
    QVector<WaterfallRow> loadRows(const WaterfallSessionId& id,
                                   qint64 fromUtcMs,
                                   qint64 toUtcMs,
                                   int maxRows) const override;
    int rowCount(const WaterfallSessionId& id) const override;

private:
    struct PendingRow
    {
        WaterfallSessionId sessionId;
        WaterfallRow row;
    };

    using IndexRecord = storage_format::WaterfallIndexRecord;

    void writerLoop();
    void appendRowSync(const WaterfallSessionId& id, const WaterfallRow& row);

    void scanExistingSessions();
    bool ensureSessionFiles(const WaterfallSessionMetadata& metadata,
                            const QString& sessionDirPath);
    void writeMetadata(const WaterfallSessionMetadata& metadata) const;
    std::optional<WaterfallSessionMetadata> readMetadata(const QString& sessionDirPath) const;

    QString sessionDirPathLocked(const WaterfallSessionId& id) const;
    qsizetype findSessionIndexLocked(const WaterfallSessionId& id) const;
    void updateMetadataLocked(const WaterfallSessionMetadata& metadata);
    void sortSessionsLocked();

    bool ensureBinHeader(QFile& file) const;
    bool ensureIndexHeader(QFile& file) const;
    bool readBinHeader(QFile& file) const;
    bool readIndexHeader(QFile& file) const;

    bool appendIndexRecord(QFile& indexFile,
                           const WaterfallRow& row,
                           quint64 fileOffset,
                           quint32 rowByteSize) const;
    QVector<IndexRecord> readIndexRecords(const QString& indexPath,
                                          bool publishDiagnostics) const;
    bool rebuildIndexFromBin(const QString& sessionDirPath) const;
    std::optional<WaterfallRow> readRowAt(QFile& binFile,
                                          const WaterfallSessionId& id,
                                          const IndexRecord& record) const;

    QVector<WaterfallRow> recentRowsFor(const WaterfallSessionId& id,
                                        qint64 fromUtcMs,
                                        qint64 toUtcMs) const;
    void rememberRecentRowLocked(const WaterfallSessionId& id, const WaterfallRow& row);
    void rotateSessionsIfNeededLocked(const WaterfallSessionId& activeSessionId);

    void publish(DiagnosticSeverity severity, const QString& message) const;
    void publishError(const QString& message) const;
    void publishWarning(const QString& message) const;

    Config m_config;
    IDiagnosticsSink* m_diagnosticsSink = nullptr;

    mutable QMutex m_mutex;
    QVector<WaterfallSessionMetadata> m_sessions;
    QHash<QString, QString> m_sessionDirs;
    QHash<QString, QVector<WaterfallRow>> m_recentRows;

    mutable std::mutex m_fileMutex;
    std::deque<PendingRow> m_writeQueue;
    std::condition_variable m_writeCondition;
    std::mutex m_writeMutex;
    std::thread m_writeThread;
    bool m_stopRequested = false;
};

} // namespace siriusscope::infrastructure
