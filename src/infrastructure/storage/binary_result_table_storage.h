#pragma once

#include "infrastructure/interfaces/diagnostics_sink.h"
#include "infrastructure/interfaces/result_table_storage.h"
#include "infrastructure/storage/result_table_storage_format.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <mutex>
#include <optional>

namespace siriusscope::infrastructure {

class BinaryResultTableStorage final : public IResultTableStorage
{
public:
    struct Config
    {
        QString dataRootPath;
        bool fsyncMetadata = false;
    };

    explicit BinaryResultTableStorage(Config config,
                                      IDiagnosticsSink* diagnosticsSink = nullptr);

    core::OperationResult append(const core::ResultTableRow& row) override;
    std::vector<core::ResultTableRow> readAll() override;

private:
    using IndexRecord = result_table_storage_format::ResultTableIndexRecord;

    QString resultTableDirPath() const;
    QString metadataPath() const;
    QString binPath() const;
    QString indexPath() const;

    bool ensureStorageDir() const;
    bool ensureBinHeader(QFile& file) const;
    bool ensureIndexHeader(QFile& file) const;
    std::optional<result_table_storage_format::ResultTableBinFileHeader> readBinHeaderValue(
        QFile& file,
        bool publishDiagnostics) const;
    std::optional<result_table_storage_format::ResultTableIndexFileHeader> readIndexHeaderValue(
        QFile& file,
        bool publishDiagnostics) const;
    bool readBinHeader(QFile& file, bool publishDiagnostics) const;
    bool readIndexHeader(QFile& file, bool publishDiagnostics) const;

    bool appendIndexRecord(QFile& indexFile,
                           const core::ResultTableRow& row,
                           quint64 fileOffset,
                           quint32 recordByteSize) const;
    void writeMetadata(int rowCount) const;

    QByteArray serializeRow(const core::ResultTableRow& row) const;
    std::optional<core::ResultTableRow> deserializeRow(const QByteArray& payload,
                                                       std::uint32_t recordVersion) const;

    void publish(DiagnosticSeverity severity, const QString& message) const;
    void publishInfo(const QString& message) const;
    void publishWarning(const QString& message) const;
    void publishError(const QString& message) const;

    Config m_config;
    IDiagnosticsSink* m_diagnosticsSink = nullptr;
    mutable std::mutex m_fileMutex;
};

} // namespace siriusscope::infrastructure
