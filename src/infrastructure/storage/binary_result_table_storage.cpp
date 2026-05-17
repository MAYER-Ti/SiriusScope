#include "infrastructure/storage/binary_result_table_storage.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace siriusscope::infrastructure {
namespace {

using namespace result_table_storage_format;

constexpr auto kMetadataSchema = "ResultTableStorage";
constexpr qsizetype kMaxRecordPayloadBytes = 16 * 1024 * 1024;

template <typename T>
bool writeStruct(QFile& file, const T& value)
{
    return file.write(reinterpret_cast<const char*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
bool readStruct(QFile& file, T* value)
{
    return file.read(reinterpret_cast<char*>(value), sizeof(T)) == sizeof(T);
}

bool magicEquals(const char* actual, const char* expected)
{
    return std::memcmp(actual, expected, 8) == 0;
}

bool isSupportedFormatVersion(std::uint32_t version)
{
    return version == kFormatVersion || version == kLegacyFormatVersion;
}

ResultTableBinFileHeader binHeader()
{
    ResultTableBinFileHeader header{};
    std::memcpy(header.magic, kResultTableBinMagic, sizeof(header.magic));
    header.formatVersion = kFormatVersion;
    header.headerSize = sizeof(ResultTableBinFileHeader);
    header.byteOrder = kByteOrderLittleEndian;
    return header;
}

ResultTableIndexFileHeader indexHeader()
{
    ResultTableIndexFileHeader header{};
    std::memcpy(header.magic, kResultTableIndexMagic, sizeof(header.magic));
    header.formatVersion = kFormatVersion;
    header.headerSize = sizeof(ResultTableIndexFileHeader);
    header.recordSize = sizeof(ResultTableIndexRecord);
    return header;
}

ResultTableRecordDiskHeader recordHeader(quint32 payloadSize)
{
    ResultTableRecordDiskHeader header{};
    header.recordMagic = kResultTableRecordMagic;
    header.recordVersion = kFormatVersion;
    header.payloadSizeBytes = payloadSize;
    header.crc32 = 0;
    return header;
}

qint64 nowUtcMs()
{
    return QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
}

QString keyFor(const core::ResultTableRow& row)
{
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<qulonglong>(row.sampleIndex))
        .arg(static_cast<qlonglong>(row.resultTimeUtcNs))
        .arg(row.bandIndex);
}

} // namespace

BinaryResultTableStorage::BinaryResultTableStorage(Config config,
                                                   IDiagnosticsSink* diagnosticsSink)
    : m_config(std::move(config))
    , m_diagnosticsSink(diagnosticsSink)
{
}

core::OperationResult BinaryResultTableStorage::append(const core::ResultTableRow& row)
{
    const auto validation = row.validate(core::defaultRuntimeCapabilities());
    if (!validation) {
        publishWarning(QStringLiteral("Rejected invalid result table row before append"));
        return core::OperationResult::failure("result table row is invalid");
    }

    std::lock_guard lock(m_fileMutex);
    if (!ensureStorageDir()) {
        return core::OperationResult::failure("cannot create result table storage directory");
    }

    QFile binFile(binPath());
    if (!binFile.open(QIODevice::ReadWrite)) {
        const auto message = QStringLiteral("Cannot open result_table.bin: %1")
                                 .arg(binFile.errorString());
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }
    if (!ensureBinHeader(binFile)) {
        const auto message = QStringLiteral("Cannot validate result_table.bin header");
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }
    if (!binFile.seek(binFile.size())) {
        const auto message = QStringLiteral("Cannot seek result_table.bin for append: %1")
                                 .arg(binFile.errorString());
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }

    const auto payload = serializeRow(row);
    if (payload.size() > kMaxRecordPayloadBytes) {
        const auto message = QStringLiteral("Result table record is too large");
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }

    const quint64 fileOffset = static_cast<quint64>(binFile.pos());
    const auto header = recordHeader(static_cast<quint32>(payload.size()));
    const quint32 recordByteSize =
        static_cast<quint32>(sizeof(ResultTableRecordDiskHeader) + payload.size());
    if (!writeStruct(binFile, header) || binFile.write(payload) != payload.size()) {
        const auto message = QStringLiteral("Cannot append result table record: %1")
                                 .arg(binFile.errorString());
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }
    binFile.flush();

    QFile indexFile(indexPath());
    if (!indexFile.open(QIODevice::ReadWrite)) {
        const auto message = QStringLiteral("Cannot open result_table.idx: %1")
                                 .arg(indexFile.errorString());
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }
    if (!ensureIndexHeader(indexFile)) {
        const auto message = QStringLiteral("Cannot validate result_table.idx header");
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }
    if (!indexFile.seek(indexFile.size())
        || !appendIndexRecord(indexFile, row, fileOffset, recordByteSize)) {
        const auto message = QStringLiteral("Cannot append result table index record: %1")
                                 .arg(indexFile.errorString());
        publishError(message);
        return core::OperationResult::failure(message.toStdString());
    }
    indexFile.flush();

    const qint64 indexBodySize =
        indexFile.size() - static_cast<qint64>(sizeof(ResultTableIndexFileHeader));
    const int rowCount = indexBodySize > 0
        ? static_cast<int>(indexBodySize / static_cast<qint64>(sizeof(ResultTableIndexRecord)))
        : 0;
    writeMetadata(rowCount);
    return core::OperationResult::ok();
}

std::vector<core::ResultTableRow> BinaryResultTableStorage::readAll()
{
    std::lock_guard lock(m_fileMutex);
    std::vector<core::ResultTableRow> rows;

    QFile binFile(binPath());
    if (!QFileInfo::exists(binPath())) {
        return rows;
    }
    if (!binFile.open(QIODevice::ReadOnly)) {
        publishWarning(QStringLiteral("Cannot open result_table.bin for read: %1")
                           .arg(binFile.errorString()));
        return rows;
    }
    if (!readBinHeader(binFile, true)) {
        return rows;
    }

    while (!binFile.atEnd()) {
        const auto recordOffset = binFile.pos();
        ResultTableRecordDiskHeader header{};
        const qint64 readSize =
            binFile.read(reinterpret_cast<char*>(&header), sizeof(ResultTableRecordDiskHeader));
        if (readSize == 0) {
            break;
        }
        if (readSize != static_cast<qint64>(sizeof(ResultTableRecordDiskHeader))) {
            publishWarning(QStringLiteral("Partial result table record header at offset %1")
                               .arg(recordOffset));
            break;
        }
        if (header.recordMagic != kResultTableRecordMagic
            || !isSupportedFormatVersion(header.recordVersion)
            || header.payloadSizeBytes > static_cast<std::uint32_t>(kMaxRecordPayloadBytes)) {
            publishWarning(QStringLiteral("Corrupted result table record at offset %1")
                               .arg(recordOffset));
            break;
        }

        const QByteArray payload =
            binFile.read(static_cast<qint64>(header.payloadSizeBytes));
        if (payload.size() != static_cast<qsizetype>(header.payloadSizeBytes)) {
            publishWarning(QStringLiteral("Partial result table payload at offset %1")
                               .arg(recordOffset));
            break;
        }

        const auto row = deserializeRow(payload, header.recordVersion);
        if (!row) {
            publishWarning(QStringLiteral("Cannot deserialize result table record at offset %1")
                               .arg(recordOffset));
            continue;
        }
        const auto validation = row->validate(core::defaultRuntimeCapabilities());
        if (!validation) {
            publishWarning(QStringLiteral("Invalid result table record skipped at offset %1")
                               .arg(recordOffset));
            continue;
        }
        rows.push_back(*row);
    }

    std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
        if (left.resultTimeUtcNs != right.resultTimeUtcNs) {
            return left.resultTimeUtcNs < right.resultTimeUtcNs;
        }
        if (left.sampleIndex != right.sampleIndex) {
            return left.sampleIndex < right.sampleIndex;
        }
        return left.bandIndex < right.bandIndex;
    });

    std::vector<core::ResultTableRow> uniqueRows;
    uniqueRows.reserve(rows.size());
    QSet<QString> keys;
    for (const auto& row : rows) {
        const auto key = keyFor(row);
        if (keys.contains(key)) {
            continue;
        }
        keys.insert(key);
        uniqueRows.push_back(row);
    }

    return uniqueRows;
}

QString BinaryResultTableStorage::resultTableDirPath() const
{
    return QDir(m_config.dataRootPath).filePath(QStringLiteral("result_table"));
}

QString BinaryResultTableStorage::metadataPath() const
{
    return QDir(resultTableDirPath()).filePath(QStringLiteral("metadata.json"));
}

QString BinaryResultTableStorage::binPath() const
{
    return QDir(resultTableDirPath()).filePath(QStringLiteral("result_table.bin"));
}

QString BinaryResultTableStorage::indexPath() const
{
    return QDir(resultTableDirPath()).filePath(QStringLiteral("result_table.idx"));
}

bool BinaryResultTableStorage::ensureStorageDir() const
{
    QDir dir;
    if (dir.mkpath(resultTableDirPath())) {
        return true;
    }

    publishError(QStringLiteral("Cannot create result table storage directory: %1")
                     .arg(resultTableDirPath()));
    return false;
}

bool BinaryResultTableStorage::ensureBinHeader(QFile& file) const
{
    if (file.size() == 0) {
        const bool written = file.seek(0) && writeStruct(file, binHeader());
        if (written) {
            publishInfo(QStringLiteral("Result table binary file created"));
        }
        return written;
    }

    auto header = readBinHeaderValue(file, true);
    if (!header) {
        return false;
    }
    if (header->formatVersion == kLegacyFormatVersion) {
        header->formatVersion = kFormatVersion;
        if (!file.seek(0) || !writeStruct(file, *header)) {
            publishError(QStringLiteral("Cannot upgrade result_table.bin header"));
            return false;
        }
        file.flush();
        publishInfo(QStringLiteral("Result table binary file upgraded to format v%1")
                        .arg(kFormatVersion));
    }
    return true;
}

bool BinaryResultTableStorage::ensureIndexHeader(QFile& file) const
{
    if (file.size() == 0) {
        return file.seek(0) && writeStruct(file, indexHeader());
    }

    auto header = readIndexHeaderValue(file, true);
    if (!header) {
        return false;
    }
    if (header->formatVersion == kLegacyFormatVersion) {
        header->formatVersion = kFormatVersion;
        if (!file.seek(0) || !writeStruct(file, *header)) {
            publishWarning(QStringLiteral("Cannot upgrade result_table.idx header"));
            return false;
        }
        file.flush();
    }
    return true;
}

std::optional<result_table_storage_format::ResultTableBinFileHeader>
BinaryResultTableStorage::readBinHeaderValue(
    QFile& file,
    bool publishDiagnostics) const
{
    if (!file.seek(0) || file.size() < static_cast<qint64>(sizeof(ResultTableBinFileHeader))) {
        if (publishDiagnostics) {
            publishError(QStringLiteral("Invalid result_table.bin header size"));
        }
        return std::nullopt;
    }

    ResultTableBinFileHeader header{};
    if (!readStruct(file, &header)) {
        if (publishDiagnostics) {
            publishError(QStringLiteral("Cannot read result_table.bin header"));
        }
        return std::nullopt;
    }
    if (!magicEquals(header.magic, kResultTableBinMagic)) {
        if (publishDiagnostics) {
            publishError(QStringLiteral("Invalid result_table.bin magic"));
        }
        return std::nullopt;
    }
    if (!isSupportedFormatVersion(header.formatVersion)) {
        if (publishDiagnostics) {
            publishError(QStringLiteral("Unsupported result_table.bin format version %1")
                             .arg(header.formatVersion));
        }
        return std::nullopt;
    }
    if (header.headerSize != sizeof(ResultTableBinFileHeader)
        || header.byteOrder != kByteOrderLittleEndian) {
        if (publishDiagnostics) {
            publishError(QStringLiteral("Invalid result_table.bin header fields"));
        }
        return std::nullopt;
    }
    return header;
}

std::optional<result_table_storage_format::ResultTableIndexFileHeader>
BinaryResultTableStorage::readIndexHeaderValue(
    QFile& file,
    bool publishDiagnostics) const
{
    if (!file.seek(0) || file.size() < static_cast<qint64>(sizeof(ResultTableIndexFileHeader))) {
        if (publishDiagnostics) {
            publishWarning(QStringLiteral("Invalid result_table.idx header size"));
        }
        return std::nullopt;
    }

    ResultTableIndexFileHeader header{};
    if (!readStruct(file, &header)) {
        if (publishDiagnostics) {
            publishWarning(QStringLiteral("Cannot read result_table.idx header"));
        }
        return std::nullopt;
    }
    const bool valid = magicEquals(header.magic, kResultTableIndexMagic)
        && isSupportedFormatVersion(header.formatVersion)
        && header.headerSize == sizeof(ResultTableIndexFileHeader)
        && header.recordSize == sizeof(ResultTableIndexRecord);
    if (!valid && publishDiagnostics) {
        publishWarning(QStringLiteral("Invalid result_table.idx header"));
    }
    if (!valid) {
        return std::nullopt;
    }
    return header;
}

bool BinaryResultTableStorage::readBinHeader(QFile& file, bool publishDiagnostics) const
{
    return readBinHeaderValue(file, publishDiagnostics).has_value();
}

bool BinaryResultTableStorage::readIndexHeader(QFile& file, bool publishDiagnostics) const
{
    return readIndexHeaderValue(file, publishDiagnostics).has_value();
}

bool BinaryResultTableStorage::appendIndexRecord(QFile& indexFile,
                                                 const core::ResultTableRow& row,
                                                 quint64 fileOffset,
                                                 quint32 recordByteSize) const
{
    ResultTableIndexRecord record{};
    record.resultTimeUtcNs = row.resultTimeUtcNs;
    record.sampleIndex = row.sampleIndex;
    record.fileOffset = fileOffset;
    record.recordByteSize = recordByteSize;
    record.bandIndex = row.bandIndex;
    return writeStruct(indexFile, record);
}

void BinaryResultTableStorage::writeMetadata(int rowCount) const
{
    QJsonObject object;
    object.insert(QStringLiteral("schema"), QString::fromLatin1(kMetadataSchema));
    object.insert(QStringLiteral("formatVersion"), static_cast<int>(kFormatVersion));
    object.insert(QStringLiteral("byteOrder"), QStringLiteral("little-endian"));
    object.insert(QStringLiteral("resultTableBinFile"), QStringLiteral("result_table.bin"));
    object.insert(QStringLiteral("resultTableIndexFile"), QStringLiteral("result_table.idx"));
    object.insert(QStringLiteral("rowCount"), rowCount);
    object.insert(QStringLiteral("updatedUtcMs"), nowUtcMs());

    QFile file(metadataPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        publishWarning(QStringLiteral("Cannot write result table metadata: %1")
                           .arg(file.errorString()));
        return;
    }
    const auto payload = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        publishWarning(QStringLiteral("Cannot write complete result table metadata: %1")
                           .arg(file.errorString()));
        return;
    }
    file.flush();
    if (m_config.fsyncMetadata) {
        file.flush();
    }
}

QByteArray BinaryResultTableStorage::serializeRow(const core::ResultTableRow& row) const
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);

    stream << static_cast<quint64>(row.sampleIndex);
    stream << static_cast<qint64>(row.resultTimeUtcNs);
    stream << row.bearingAzimuthDeg;
    stream << row.antennaAzimuthDeg;
    stream << static_cast<qint32>(row.bandIndex);
    stream << static_cast<qint32>(row.quality ? 1 : 0);
    stream << (row.quality ? *row.quality : 0.0);

    stream << static_cast<quint32>(row.frequenciesHz.size());
    for (const auto frequencyHz : row.frequenciesHz) {
        stream << static_cast<qint64>(frequencyHz);
    }

    stream << static_cast<quint32>(row.diagnostics.size());
    for (const auto& diagnostic : row.diagnostics) {
        const QByteArray message = QByteArray::fromStdString(diagnostic.message);
        stream << static_cast<quint32>(diagnostic.code);
        stream << static_cast<quint32>(message.size());
        stream.writeRawData(message.constData(), message.size());
    }

    return payload;
}

std::optional<core::ResultTableRow> BinaryResultTableStorage::deserializeRow(
    const QByteArray& payload,
    std::uint32_t recordVersion) const
{
    QDataStream stream(payload);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);

    quint64 sampleIndex = 0;
    qint64 resultTimeUtcNs = 0;
    double bearingAzimuthDeg = 0.0;
    double antennaAzimuthDeg = 0.0;
    qint32 bandIndex = 0;
    qint32 qualityState = 0;
    double qualityValue = 0.0;
    quint32 frequencyCount = 0;

    stream >> sampleIndex;
    stream >> resultTimeUtcNs;
    if (recordVersion == kLegacyFormatVersion) {
        stream >> antennaAzimuthDeg;
        bearingAzimuthDeg = antennaAzimuthDeg;
    } else {
        stream >> bearingAzimuthDeg;
        stream >> antennaAzimuthDeg;
    }
    stream >> bandIndex;
    stream >> qualityState;
    stream >> qualityValue;
    stream >> frequencyCount;
    if (stream.status() != QDataStream::Ok || frequencyCount > 4096) {
        return std::nullopt;
    }

    std::vector<std::int64_t> frequenciesHz;
    frequenciesHz.reserve(frequencyCount);
    for (quint32 i = 0; i < frequencyCount; ++i) {
        qint64 frequencyHz = 0;
        stream >> frequencyHz;
        if (stream.status() != QDataStream::Ok) {
            return std::nullopt;
        }
        frequenciesHz.push_back(static_cast<std::int64_t>(frequencyHz));
    }

    quint32 diagnosticCount = 0;
    stream >> diagnosticCount;
    if (stream.status() != QDataStream::Ok || diagnosticCount > 4096) {
        return std::nullopt;
    }

    std::vector<core::ValidationIssue> diagnostics;
    diagnostics.reserve(diagnosticCount);
    for (quint32 i = 0; i < diagnosticCount; ++i) {
        quint32 code = 0;
        quint32 messageSize = 0;
        stream >> code;
        stream >> messageSize;
        if (stream.status() != QDataStream::Ok
            || messageSize > static_cast<quint32>(kMaxRecordPayloadBytes)) {
            return std::nullopt;
        }
        QByteArray message;
        message.resize(static_cast<qsizetype>(messageSize));
        if (messageSize > 0
            && stream.readRawData(message.data(), message.size()) != message.size()) {
            return std::nullopt;
        }
        diagnostics.push_back(core::ValidationIssue{
            static_cast<core::ValidationCode>(code),
            message.toStdString(),
        });
    }

    std::optional<double> quality;
    if (qualityState == 1) {
        quality = qualityValue;
    } else if (qualityState != 0) {
        publishWarning(QStringLiteral("Unknown result table quality state %1")
                           .arg(qualityState));
    }

    return core::ResultTableRow{
        static_cast<std::uint64_t>(sampleIndex),
        static_cast<std::int64_t>(resultTimeUtcNs),
        bearingAzimuthDeg,
        antennaAzimuthDeg,
        static_cast<int>(bandIndex),
        std::move(frequenciesHz),
        quality,
        std::move(diagnostics),
    };
}

void BinaryResultTableStorage::publish(DiagnosticSeverity severity, const QString& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(DiagnosticEvent{
        severity,
        "ResultTableStorage",
        message.toStdString(),
        std::chrono::system_clock::now(),
    });
}

void BinaryResultTableStorage::publishInfo(const QString& message) const
{
    publish(DiagnosticSeverity::Info, message);
}

void BinaryResultTableStorage::publishWarning(const QString& message) const
{
    publish(DiagnosticSeverity::Warning, message);
}

void BinaryResultTableStorage::publishError(const QString& message) const
{
    publish(DiagnosticSeverity::Error, message);
}

} // namespace siriusscope::infrastructure
