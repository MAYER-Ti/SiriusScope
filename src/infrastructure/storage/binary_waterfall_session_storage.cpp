#include "infrastructure/storage/binary_waterfall_session_storage.h"

#include "infrastructure/storage/waterfall_storage_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>
#include <QVariant>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

namespace siriusscope::infrastructure {
namespace {

constexpr int kRecentRowsPerSession = 4096;
constexpr auto kMetadataSchema = "WaterfallSessionStorage";
constexpr auto kCreatedBy = "SiriusScope";
constexpr auto kByteOrder = "little-endian";
constexpr auto kWaterfallBinFileName = "waterfall.bin";
constexpr auto kWaterfallIndexFileName = "waterfall.idx";

using namespace storage_format;

bool rowEarlier(const WaterfallRow& lhs, const WaterfallRow& rhs)
{
    if (lhs.utcMs != rhs.utcMs) {
        return lhs.utcMs < rhs.utcMs;
    }
    return lhs.firstSampleIndex < rhs.firstSampleIndex;
}

bool indexEarlier(const WaterfallIndexRecord& lhs, const WaterfallIndexRecord& rhs)
{
    if (lhs.utcMs != rhs.utcMs) {
        return lhs.utcMs < rhs.utcMs;
    }
    return lhs.firstSampleIndex < rhs.firstSampleIndex;
}

bool sessionEarlier(const WaterfallSessionMetadata& lhs,
                    const WaterfallSessionMetadata& rhs)
{
    if (lhs.startUtcMs != rhs.startUtcMs) {
        return lhs.startUtcMs < rhs.startUtcMs;
    }
    return lhs.id.value < rhs.id.value;
}

WaterfallSessionId generatedSessionId(qint64 startUtcMs)
{
    return WaterfallSessionId{QStringLiteral("session-%1").arg(startUtcMs)};
}

template <typename T>
bool writeStruct(QFileDevice& file, const T& value)
{
    return file.write(reinterpret_cast<const char*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
bool readStruct(QFileDevice& file, T* value)
{
    return value
        && file.read(reinterpret_cast<char*>(value), sizeof(T)) == sizeof(T);
}

bool magicEquals(const char* lhs, const char* rhs)
{
    return std::memcmp(lhs, rhs, 8) == 0;
}

WaterfallBinFileHeader binHeader()
{
    WaterfallBinFileHeader header{};
    std::memcpy(header.magic, kWaterfallBinMagic, sizeof(header.magic));
    header.formatVersion = kFormatVersion;
    header.headerSize = sizeof(WaterfallBinFileHeader);
    header.binRecordSize = sizeof(WaterfallBeamBinDisk);
    return header;
}

WaterfallIndexFileHeader indexHeader()
{
    WaterfallIndexFileHeader header{};
    std::memcpy(header.magic, kWaterfallIndexMagic, sizeof(header.magic));
    header.formatVersion = kFormatVersion;
    header.headerSize = sizeof(WaterfallIndexFileHeader);
    header.recordSize = sizeof(WaterfallIndexRecord);
    return header;
}

QJsonValue int64Value(qint64 value)
{
    return QJsonValue::fromVariant(QVariant::fromValue(value));
}

qint64 int64FromJson(const QJsonObject& object, const QString& key, qint64 defaultValue = 0)
{
    bool ok = false;
    const qint64 value = object.value(key).toVariant().toLongLong(&ok);
    return ok ? value : defaultValue;
}

QJsonObject toJson(const WaterfallSessionMetadata& metadata)
{
    QJsonObject object;
    object.insert(QStringLiteral("formatVersion"), static_cast<int>(kFormatVersion));
    object.insert(QStringLiteral("schema"), QString::fromLatin1(kMetadataSchema));
    object.insert(QStringLiteral("createdBy"), QString::fromLatin1(kCreatedBy));
    object.insert(QStringLiteral("byteOrder"), QString::fromLatin1(kByteOrder));
    object.insert(QStringLiteral("rowRecordVersion"), static_cast<int>(kFormatVersion));
    object.insert(QStringLiteral("id"), metadata.id.value);
    object.insert(QStringLiteral("startUtcMs"), int64Value(metadata.startUtcMs));
    object.insert(QStringLiteral("endUtcMs"), int64Value(metadata.endUtcMs));
    object.insert(QStringLiteral("rowPeriodMs"), int64Value(metadata.rowPeriodMs));
    object.insert(QStringLiteral("binCount"), metadata.binCount);
    object.insert(QStringLiteral("bandCount"), metadata.bandCount);
    object.insert(QStringLiteral("beamCount"), metadata.beamCount);
    object.insert(QStringLiteral("sourceName"), metadata.sourceName);
    object.insert(QStringLiteral("closed"), metadata.closed);
    object.insert(QStringLiteral("waterfallBinFile"), QString::fromLatin1(kWaterfallBinFileName));
    object.insert(QStringLiteral("waterfallIndexFile"),
                  QString::fromLatin1(kWaterfallIndexFileName));
    return object;
}

std::optional<WaterfallSessionMetadata> fromJson(const QJsonObject& object)
{
    if (object.value(QStringLiteral("formatVersion")).toInt() != static_cast<int>(kFormatVersion)) {
        return std::nullopt;
    }

    WaterfallSessionMetadata metadata;
    metadata.id = WaterfallSessionId{object.value(QStringLiteral("id")).toString()};
    if (!metadata.id.isValid()) {
        return std::nullopt;
    }

    metadata.startUtcMs = int64FromJson(object, QStringLiteral("startUtcMs"));
    metadata.endUtcMs = int64FromJson(object, QStringLiteral("endUtcMs"), metadata.startUtcMs);
    metadata.rowPeriodMs = std::max<qint64>(
        1,
        int64FromJson(object, QStringLiteral("rowPeriodMs"), 1000));
    metadata.binCount = object.value(QStringLiteral("binCount")).toInt(1024);
    metadata.bandCount = object.value(QStringLiteral("bandCount")).toInt(5);
    metadata.beamCount = object.value(QStringLiteral("beamCount")).toInt(2);
    metadata.sourceName = object.value(QStringLiteral("sourceName")).toString();
    metadata.closed = object.value(QStringLiteral("closed")).toBool(false);
    return metadata;
}

QString rowKey(qint64 utcMs, quint64 firstSampleIndex)
{
    return QStringLiteral("%1:%2").arg(utcMs).arg(firstSampleIndex);
}

QString rowKey(const WaterfallRow& row)
{
    return rowKey(row.utcMs, row.firstSampleIndex);
}

QString rowKey(const WaterfallIndexRecord& record)
{
    return rowKey(static_cast<qint64>(record.utcMs), record.firstSampleIndex);
}

bool inRange(qint64 utcMs, qint64 fromUtcMs, qint64 toUtcMs)
{
    const qint64 minUtc = std::min(fromUtcMs, toUtcMs);
    const qint64 maxUtc = std::max(fromUtcMs, toUtcMs);
    return utcMs >= minUtc && utcMs <= maxUtc;
}

} // namespace

BinaryWaterfallSessionStorage::BinaryWaterfallSessionStorage(
    Config config,
    IDiagnosticsSink* diagnosticsSink)
    : m_config(std::move(config))
    , m_diagnosticsSink(diagnosticsSink)
{
    if (m_config.dataRootPath.isEmpty()) {
        m_config.dataRootPath = QStringLiteral("SiriusScopeData");
    }
    m_config.maxSessions = std::max(1, m_config.maxSessions);

    scanExistingSessions();
    m_writeThread = std::thread(&BinaryWaterfallSessionStorage::writerLoop, this);
}

BinaryWaterfallSessionStorage::~BinaryWaterfallSessionStorage()
{
    {
        std::lock_guard lock(m_writeMutex);
        m_stopRequested = true;
    }
    m_writeCondition.notify_all();

    if (m_writeThread.joinable()) {
        m_writeThread.join();
    }
}

QVector<WaterfallSessionMetadata> BinaryWaterfallSessionStorage::listSessions() const
{
    QMutexLocker lock(&m_mutex);
    auto sessions = m_sessions;
    std::sort(sessions.begin(), sessions.end(), sessionEarlier);
    return sessions;
}

std::optional<WaterfallSessionMetadata> BinaryWaterfallSessionStorage::session(
    const WaterfallSessionId& id) const
{
    QMutexLocker lock(&m_mutex);
    const qsizetype index = findSessionIndexLocked(id);
    if (index < 0) {
        return std::nullopt;
    }
    return m_sessions.at(index);
}

std::optional<WaterfallSessionMetadata> BinaryWaterfallSessionStorage::latestSession() const
{
    QMutexLocker lock(&m_mutex);
    if (m_sessions.isEmpty()) {
        return std::nullopt;
    }

    return *std::max_element(m_sessions.cbegin(), m_sessions.cend(), sessionEarlier);
}

std::optional<WaterfallSessionMetadata> BinaryWaterfallSessionStorage::previousSession(
    const WaterfallSessionId& id) const
{
    QMutexLocker lock(&m_mutex);
    const qsizetype currentIndex = findSessionIndexLocked(id);
    if (currentIndex < 0) {
        if (m_sessions.isEmpty()) {
            return std::nullopt;
        }
        return *std::max_element(m_sessions.cbegin(), m_sessions.cend(), sessionEarlier);
    }

    const auto& current = m_sessions.at(currentIndex);
    std::optional<WaterfallSessionMetadata> previous;
    for (const auto& candidate : m_sessions) {
        if (candidate.id == id || !sessionEarlier(candidate, current)) {
            continue;
        }
        if (!previous || sessionEarlier(*previous, candidate)) {
            previous = candidate;
        }
    }
    return previous;
}

std::optional<WaterfallSessionMetadata> BinaryWaterfallSessionStorage::nextSession(
    const WaterfallSessionId& id) const
{
    QMutexLocker lock(&m_mutex);
    const qsizetype currentIndex = findSessionIndexLocked(id);
    if (currentIndex < 0) {
        return std::nullopt;
    }

    const auto& current = m_sessions.at(currentIndex);
    std::optional<WaterfallSessionMetadata> next;
    for (const auto& candidate : m_sessions) {
        if (candidate.id == id || !sessionEarlier(current, candidate)) {
            continue;
        }
        if (!next || sessionEarlier(candidate, *next)) {
            next = candidate;
        }
    }
    return next;
}

WaterfallSessionMetadata BinaryWaterfallSessionStorage::startSession(
    WaterfallSessionMetadata metadata)
{
    if (!metadata.id.isValid()) {
        metadata.id = generatedSessionId(metadata.startUtcMs);
    }
    if (metadata.startUtcMs <= 0) {
        metadata.startUtcMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (metadata.endUtcMs <= 0) {
        metadata.endUtcMs = metadata.startUtcMs;
    }
    metadata.rowPeriodMs = std::max<qint64>(1, metadata.rowPeriodMs);
    metadata.closed = false;

    const QString recordingsRoot =
        waterfall_storage_paths::recordingsRootPath(m_config.dataRootPath);
    const QString sessionDirPath = QDir(recordingsRoot).filePath(
        waterfall_storage_paths::safeSessionDirectoryName(metadata));

    ensureSessionFiles(metadata, sessionDirPath);

    {
        QMutexLocker lock(&m_mutex);
        updateMetadataLocked(metadata);
        m_sessionDirs.insert(metadata.id.value, sessionDirPath);
        m_recentRows.insert(metadata.id.value, {});
        sortSessionsLocked();
        rotateSessionsIfNeededLocked(metadata.id);
    }

    writeMetadata(metadata);
    return metadata;
}

bool BinaryWaterfallSessionStorage::closeSession(const WaterfallSessionId& id, qint64 endUtcMs)
{
    WaterfallSessionMetadata metadata;
    {
        QMutexLocker lock(&m_mutex);
        const qsizetype index = findSessionIndexLocked(id);
        if (index < 0) {
            return false;
        }

        auto& stored = m_sessions[index];
        stored.endUtcMs = std::max(stored.endUtcMs, endUtcMs);
        stored.closed = true;
        metadata = stored;
    }

    writeMetadata(metadata);
    return true;
}

void BinaryWaterfallSessionStorage::appendRow(const WaterfallSessionId& id,
                                              const WaterfallRow& row)
{
    if (!id.isValid()) {
        publishWarning(QStringLiteral("Waterfall append skipped: empty session id"));
        return;
    }

    {
        QMutexLocker lock(&m_mutex);
        const qsizetype index = findSessionIndexLocked(id);
        if (index < 0) {
            publishWarning(QStringLiteral("Waterfall append skipped: unknown session %1")
                               .arg(id.value));
            return;
        }

        WaterfallRow stored = row;
        stored.sessionId = id;
        rememberRecentRowLocked(id, stored);

        auto& metadata = m_sessions[index];
        metadata.startUtcMs = metadata.startUtcMs > 0
            ? std::min(metadata.startUtcMs, stored.utcMs)
            : stored.utcMs;
        metadata.endUtcMs = std::max(metadata.endUtcMs, stored.utcMs);
    }

    {
        std::lock_guard lock(m_writeMutex);
        WaterfallRow stored = row;
        stored.sessionId = id;
        m_writeQueue.push_back(PendingRow{id, std::move(stored)});
    }
    m_writeCondition.notify_one();
}

QVector<WaterfallRow> BinaryWaterfallSessionStorage::loadRows(
    const WaterfallSessionId& id,
    qint64 fromUtcMs,
    qint64 toUtcMs,
    int maxRows) const
{
    QString sessionDirPath;
    {
        QMutexLocker lock(&m_mutex);
        if (findSessionIndexLocked(id) < 0) {
            return {};
        }
        sessionDirPath = sessionDirPathLocked(id);
    }

    QVector<WaterfallRow> rows;
    QVector<IndexRecord> selectedRecords;

    {
        std::lock_guard fileLock(m_fileMutex);

        const QString indexFilePath = waterfall_storage_paths::indexPath(sessionDirPath);
        auto indexRecords = readIndexRecords(indexFilePath, true);
        std::sort(indexRecords.begin(), indexRecords.end(), indexEarlier);

        for (const auto& record : indexRecords) {
            const qint64 utcMs = static_cast<qint64>(record.utcMs);
            if (inRange(utcMs, fromUtcMs, toUtcMs)) {
                selectedRecords.push_back(record);
            }
        }

        if (maxRows > 0 && selectedRecords.size() > maxRows) {
            selectedRecords.erase(selectedRecords.begin(),
                                  selectedRecords.begin() + (selectedRecords.size() - maxRows));
        }

        QFile binFile(waterfall_storage_paths::binPath(sessionDirPath));
        if (binFile.open(QIODevice::ReadOnly) && readBinHeader(binFile)) {
            rows.reserve(selectedRecords.size());
            for (const auto& record : selectedRecords) {
                const auto row = readRowAt(binFile, id, record);
                if (row) {
                    rows.push_back(*row);
                }
            }
        } else if (QFileInfo::exists(waterfall_storage_paths::binPath(sessionDirPath))) {
            publishWarning(QStringLiteral("Cannot read waterfall.bin for session %1").arg(id.value));
        }
    }

    rows += recentRowsFor(id, fromUtcMs, toUtcMs);
    std::sort(rows.begin(), rows.end(), rowEarlier);

    QVector<WaterfallRow> uniqueRows;
    uniqueRows.reserve(rows.size());
    QSet<QString> seen;
    for (const auto& row : rows) {
        const QString key = rowKey(row);
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        uniqueRows.push_back(row);
    }

    if (maxRows > 0 && uniqueRows.size() > maxRows) {
        uniqueRows.erase(uniqueRows.begin(),
                         uniqueRows.begin() + (uniqueRows.size() - maxRows));
    }

    return uniqueRows;
}

int BinaryWaterfallSessionStorage::rowCount(const WaterfallSessionId& id) const
{
    return loadRows(id,
                    std::numeric_limits<qint64>::min(),
                    std::numeric_limits<qint64>::max(),
                    0)
        .size();
}

void BinaryWaterfallSessionStorage::writerLoop()
{
    for (;;) {
        std::deque<PendingRow> batch;
        {
            std::unique_lock lock(m_writeMutex);
            m_writeCondition.wait(lock, [this] {
                return m_stopRequested || !m_writeQueue.empty();
            });

            if (m_stopRequested && m_writeQueue.empty()) {
                break;
            }

            batch.swap(m_writeQueue);
        }

        for (const auto& item : batch) {
            appendRowSync(item.sessionId, item.row);
        }
    }
}

void BinaryWaterfallSessionStorage::appendRowSync(const WaterfallSessionId& id,
                                                  const WaterfallRow& row)
{
    const QString sessionDirPath = [&] {
        QMutexLocker lock(&m_mutex);
        return sessionDirPathLocked(id);
    }();

    if (sessionDirPath.isEmpty()) {
        publishWarning(QStringLiteral("Waterfall append skipped: missing session directory %1")
                           .arg(id.value));
        return;
    }

    {
        std::lock_guard fileLock(m_fileMutex);

        QFile binFile(waterfall_storage_paths::binPath(sessionDirPath));
        if (!binFile.open(QIODevice::ReadWrite)) {
            publishError(QStringLiteral("Cannot open waterfall.bin for append: %1")
                             .arg(binFile.errorString()));
            return;
        }
        if (!ensureBinHeader(binFile)) {
            publishError(QStringLiteral("Cannot validate waterfall.bin header: %1")
                             .arg(binFile.fileName()));
            return;
        }
        if (!binFile.seek(binFile.size())) {
            publishError(QStringLiteral("Cannot seek waterfall.bin for append: %1")
                             .arg(binFile.errorString()));
            return;
        }

        const quint64 fileOffset = static_cast<quint64>(binFile.pos());
        const quint32 payloadSize = static_cast<quint32>(
            row.bins.size() * sizeof(WaterfallBeamBinDisk));

        WaterfallRowDiskHeader rowHeader{};
        rowHeader.recordMagic = kWaterfallRowMagic;
        rowHeader.recordVersion = kFormatVersion;
        rowHeader.utcMs = static_cast<std::uint64_t>(std::max<qint64>(0, row.utcMs));
        rowHeader.firstSampleIndex = row.firstSampleIndex;
        rowHeader.lastSampleIndex = row.lastSampleIndex;
        rowHeader.viewMinHz = row.viewMinHz;
        rowHeader.viewMaxHz = row.viewMaxHz;
        rowHeader.binCount = static_cast<std::uint32_t>(row.bins.size());
        rowHeader.payloadSizeBytes = payloadSize;
        rowHeader.crc32 = 0;

        if (!writeStruct(binFile, rowHeader)) {
            publishError(QStringLiteral("Cannot write waterfall row header: %1")
                             .arg(binFile.errorString()));
            return;
        }

        for (const auto& bin : row.bins) {
            const WaterfallBeamBinDisk diskBin{bin.left, bin.right};
            if (!writeStruct(binFile, diskBin)) {
                publishError(QStringLiteral("Cannot write waterfall row payload: %1")
                                 .arg(binFile.errorString()));
                return;
            }
        }
        binFile.flush();

        const quint64 endOffset = static_cast<quint64>(binFile.pos());
        const quint32 rowByteSize = static_cast<quint32>(endOffset - fileOffset);

        QFile indexFile(waterfall_storage_paths::indexPath(sessionDirPath));
        if (!indexFile.open(QIODevice::ReadWrite)) {
            publishError(QStringLiteral("Cannot open waterfall.idx for append: %1")
                             .arg(indexFile.errorString()));
            return;
        }
        if (!ensureIndexHeader(indexFile)) {
            publishError(QStringLiteral("Cannot validate waterfall.idx header: %1")
                             .arg(indexFile.fileName()));
            return;
        }
        if (!indexFile.seek(indexFile.size())) {
            publishError(QStringLiteral("Cannot seek waterfall.idx for append: %1")
                             .arg(indexFile.errorString()));
            return;
        }
        if (!appendIndexRecord(indexFile, row, fileOffset, rowByteSize)) {
            publishError(QStringLiteral("Cannot append waterfall index record: %1")
                             .arg(indexFile.errorString()));
            return;
        }
        indexFile.flush();
    }

    const auto metadata = session(id);
    if (metadata) {
        writeMetadata(*metadata);
    }
}

void BinaryWaterfallSessionStorage::scanExistingSessions()
{
    const QString recordingsRoot =
        waterfall_storage_paths::recordingsRootPath(m_config.dataRootPath);
    QDir rootDir(recordingsRoot);
    if (!rootDir.exists()) {
        return;
    }

    QVector<WaterfallSessionMetadata> sessions;
    QHash<QString, QString> sessionDirs;

    const auto directories = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                   QDir::Name);
    for (const auto& directory : directories) {
        const QString sessionDirPath = directory.absoluteFilePath();
        const auto metadata = readMetadata(sessionDirPath);
        if (!metadata) {
            publishWarning(QStringLiteral("Waterfall session skipped: invalid metadata in %1")
                               .arg(sessionDirPath));
            continue;
        }

        const QString binFilePath = waterfall_storage_paths::binPath(sessionDirPath);
        if (!QFileInfo::exists(binFilePath)) {
            publishWarning(QStringLiteral("Waterfall session skipped: missing waterfall.bin in %1")
                               .arg(sessionDirPath));
            continue;
        }

        const QString indexFilePath = waterfall_storage_paths::indexPath(sessionDirPath);
        bool indexOk = false;
        QFile indexFile(indexFilePath);
        if (indexFile.open(QIODevice::ReadOnly) && readIndexHeader(indexFile)) {
            const qint64 bodySize =
                indexFile.size() - static_cast<qint64>(sizeof(WaterfallIndexFileHeader));
            indexOk = bodySize >= 0
                && bodySize % static_cast<qint64>(sizeof(WaterfallIndexRecord)) == 0;
        }

        if (!indexOk && !rebuildIndexFromBin(sessionDirPath)) {
            publishWarning(QStringLiteral("Waterfall session skipped: cannot rebuild index in %1")
                               .arg(sessionDirPath));
            continue;
        }

        sessions.push_back(*metadata);
        sessionDirs.insert(metadata->id.value, sessionDirPath);
    }

    std::sort(sessions.begin(), sessions.end(), sessionEarlier);

    QMutexLocker lock(&m_mutex);
    m_sessions = std::move(sessions);
    m_sessionDirs = std::move(sessionDirs);
}

bool BinaryWaterfallSessionStorage::ensureSessionFiles(
    const WaterfallSessionMetadata& metadata,
    const QString& sessionDirPath)
{
    QDir rootDir(waterfall_storage_paths::recordingsRootPath(m_config.dataRootPath));
    if (!rootDir.exists() && !rootDir.mkpath(QStringLiteral("."))) {
        publishError(QStringLiteral("Cannot create recordings directory: %1")
                         .arg(rootDir.absolutePath()));
        return false;
    }

    QDir sessionDir(sessionDirPath);
    if (!sessionDir.exists() && !QDir().mkpath(sessionDirPath)) {
        publishError(QStringLiteral("Cannot create waterfall session directory: %1")
                         .arg(sessionDirPath));
        return false;
    }

    std::lock_guard fileLock(m_fileMutex);

    QFile binFile(waterfall_storage_paths::binPath(sessionDirPath));
    if (!binFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || !writeStruct(binFile, binHeader())) {
        publishError(QStringLiteral("Cannot create waterfall.bin for session %1: %2")
                         .arg(metadata.id.value, binFile.errorString()));
        return false;
    }

    QFile indexFile(waterfall_storage_paths::indexPath(sessionDirPath));
    if (!indexFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || !writeStruct(indexFile, indexHeader())) {
        publishError(QStringLiteral("Cannot create waterfall.idx for session %1: %2")
                         .arg(metadata.id.value, indexFile.errorString()));
        return false;
    }

    return true;
}

void BinaryWaterfallSessionStorage::writeMetadata(
    const WaterfallSessionMetadata& metadata) const
{
    const QString sessionDirPath = [&] {
        QMutexLocker lock(&m_mutex);
        return sessionDirPathLocked(metadata.id);
    }();
    if (sessionDirPath.isEmpty()) {
        return;
    }

    QDir().mkpath(sessionDirPath);

    QSaveFile file(waterfall_storage_paths::metadataPath(sessionDirPath));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        publishError(QStringLiteral("Cannot write metadata.json: %1").arg(file.errorString()));
        return;
    }

    const QJsonDocument document(toJson(metadata));
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        publishError(QStringLiteral("Cannot serialize metadata.json: %1")
                         .arg(file.errorString()));
        file.cancelWriting();
        return;
    }

    if (m_config.fsyncMetadata) {
        file.flush();
    }

    if (!file.commit()) {
        publishError(QStringLiteral("Cannot commit metadata.json: %1").arg(file.errorString()));
    }
}

std::optional<WaterfallSessionMetadata> BinaryWaterfallSessionStorage::readMetadata(
    const QString& sessionDirPath) const
{
    QFile file(waterfall_storage_paths::metadataPath(sessionDirPath));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    return fromJson(document.object());
}

QString BinaryWaterfallSessionStorage::sessionDirPathLocked(
    const WaterfallSessionId& id) const
{
    return m_sessionDirs.value(id.value);
}

qsizetype BinaryWaterfallSessionStorage::findSessionIndexLocked(
    const WaterfallSessionId& id) const
{
    if (!id.isValid()) {
        return -1;
    }

    for (qsizetype i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void BinaryWaterfallSessionStorage::updateMetadataLocked(
    const WaterfallSessionMetadata& metadata)
{
    const qsizetype index = findSessionIndexLocked(metadata.id);
    if (index >= 0) {
        m_sessions[index] = metadata;
    } else {
        m_sessions.push_back(metadata);
    }
}

void BinaryWaterfallSessionStorage::sortSessionsLocked()
{
    std::sort(m_sessions.begin(), m_sessions.end(), sessionEarlier);
}

bool BinaryWaterfallSessionStorage::ensureBinHeader(QFile& file) const
{
    if (file.size() == 0) {
        return file.seek(0) && writeStruct(file, binHeader());
    }
    return readBinHeader(file);
}

bool BinaryWaterfallSessionStorage::ensureIndexHeader(QFile& file) const
{
    if (file.size() == 0) {
        return file.seek(0) && writeStruct(file, indexHeader());
    }
    return readIndexHeader(file);
}

bool BinaryWaterfallSessionStorage::readBinHeader(QFile& file) const
{
    if (!file.seek(0) || file.size() < static_cast<qint64>(sizeof(WaterfallBinFileHeader))) {
        return false;
    }

    WaterfallBinFileHeader header{};
    if (!readStruct(file, &header)) {
        return false;
    }

    return magicEquals(header.magic, kWaterfallBinMagic)
        && header.formatVersion == kFormatVersion
        && header.headerSize == sizeof(WaterfallBinFileHeader)
        && header.binRecordSize == sizeof(WaterfallBeamBinDisk);
}

bool BinaryWaterfallSessionStorage::readIndexHeader(QFile& file) const
{
    if (!file.seek(0) || file.size() < static_cast<qint64>(sizeof(WaterfallIndexFileHeader))) {
        return false;
    }

    WaterfallIndexFileHeader header{};
    if (!readStruct(file, &header)) {
        return false;
    }

    return magicEquals(header.magic, kWaterfallIndexMagic)
        && header.formatVersion == kFormatVersion
        && header.headerSize == sizeof(WaterfallIndexFileHeader)
        && header.recordSize == sizeof(WaterfallIndexRecord);
}

bool BinaryWaterfallSessionStorage::appendIndexRecord(QFile& indexFile,
                                                      const WaterfallRow& row,
                                                      quint64 fileOffset,
                                                      quint32 rowByteSize) const
{
    WaterfallIndexRecord record{};
    record.utcMs = static_cast<std::uint64_t>(std::max<qint64>(0, row.utcMs));
    record.firstSampleIndex = row.firstSampleIndex;
    record.lastSampleIndex = row.lastSampleIndex;
    record.fileOffset = fileOffset;
    record.rowByteSize = rowByteSize;
    record.binCount = static_cast<std::uint32_t>(row.bins.size());
    return writeStruct(indexFile, record);
}

QVector<BinaryWaterfallSessionStorage::IndexRecord>
BinaryWaterfallSessionStorage::readIndexRecords(
    const QString& indexPath,
    bool publishDiagnostics) const
{
    QVector<IndexRecord> records;
    QFile file(indexPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (publishDiagnostics && QFileInfo::exists(indexPath)) {
            publishWarning(QStringLiteral("Cannot open waterfall.idx: %1")
                               .arg(file.errorString()));
        }
        return records;
    }

    if (!readIndexHeader(file)) {
        if (publishDiagnostics) {
            publishWarning(QStringLiteral("Invalid waterfall.idx header: %1").arg(indexPath));
        }
        return records;
    }

    while (!file.atEnd()) {
        IndexRecord record{};
        const qint64 readSize =
            file.read(reinterpret_cast<char*>(&record), sizeof(IndexRecord));
        if (readSize == 0) {
            break;
        }
        if (readSize != static_cast<qint64>(sizeof(IndexRecord))) {
            if (publishDiagnostics) {
                publishWarning(QStringLiteral("Truncated waterfall.idx record: %1").arg(indexPath));
            }
            break;
        }
        records.push_back(record);
    }

    return records;
}

bool BinaryWaterfallSessionStorage::rebuildIndexFromBin(const QString& sessionDirPath) const
{
    std::lock_guard fileLock(m_fileMutex);

    QFile binFile(waterfall_storage_paths::binPath(sessionDirPath));
    if (!binFile.open(QIODevice::ReadOnly) || !readBinHeader(binFile)) {
        return false;
    }

    QFile indexFile(waterfall_storage_paths::indexPath(sessionDirPath));
    if (!indexFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || !writeStruct(indexFile, indexHeader())) {
        return false;
    }

    qint64 offset = sizeof(WaterfallBinFileHeader);
    while (offset + static_cast<qint64>(sizeof(WaterfallRowDiskHeader)) <= binFile.size()) {
        if (!binFile.seek(offset)) {
            break;
        }

        WaterfallRowDiskHeader rowHeader{};
        if (!readStruct(binFile, &rowHeader)
            || rowHeader.recordMagic != kWaterfallRowMagic
            || rowHeader.recordVersion != kFormatVersion) {
            break;
        }

        const quint64 expectedPayloadSize =
            static_cast<quint64>(rowHeader.binCount) * sizeof(WaterfallBeamBinDisk);
        if (rowHeader.payloadSizeBytes != expectedPayloadSize) {
            break;
        }

        const quint64 rowByteSize = sizeof(WaterfallRowDiskHeader) + expectedPayloadSize;
        if (rowByteSize > std::numeric_limits<quint32>::max()
            || offset + static_cast<qint64>(rowByteSize) > binFile.size()) {
            break;
        }

        WaterfallIndexRecord record{};
        record.utcMs = rowHeader.utcMs;
        record.firstSampleIndex = rowHeader.firstSampleIndex;
        record.lastSampleIndex = rowHeader.lastSampleIndex;
        record.fileOffset = static_cast<quint64>(offset);
        record.rowByteSize = static_cast<quint32>(rowByteSize);
        record.binCount = rowHeader.binCount;
        if (!writeStruct(indexFile, record)) {
            return false;
        }

        offset += static_cast<qint64>(rowByteSize);
    }

    return true;
}

std::optional<WaterfallRow> BinaryWaterfallSessionStorage::readRowAt(
    QFile& binFile,
    const WaterfallSessionId& id,
    const IndexRecord& record) const
{
    if (!binFile.seek(static_cast<qint64>(record.fileOffset))) {
        return std::nullopt;
    }

    WaterfallRowDiskHeader header{};
    if (!readStruct(binFile, &header)
        || header.recordMagic != kWaterfallRowMagic
        || header.recordVersion != kFormatVersion
        || header.binCount != record.binCount
        || header.payloadSizeBytes != record.binCount * sizeof(WaterfallBeamBinDisk)) {
        publishWarning(QStringLiteral("Invalid waterfall row at offset %1")
                           .arg(record.fileOffset));
        return std::nullopt;
    }

    WaterfallRow row;
    row.sessionId = id;
    row.utcMs = static_cast<qint64>(header.utcMs);
    row.firstSampleIndex = header.firstSampleIndex;
    row.lastSampleIndex = header.lastSampleIndex;
    row.viewMinHz = header.viewMinHz;
    row.viewMaxHz = header.viewMaxHz;
    row.bins.reserve(static_cast<qsizetype>(header.binCount));

    for (std::uint32_t i = 0; i < header.binCount; ++i) {
        WaterfallBeamBinDisk diskBin{};
        if (!readStruct(binFile, &diskBin)) {
            publishWarning(QStringLiteral("Truncated waterfall row payload at offset %1")
                               .arg(record.fileOffset));
            return std::nullopt;
        }
        row.bins.push_back(WaterfallBeamBin{diskBin.left, diskBin.right});
    }

    return row;
}

QVector<WaterfallRow> BinaryWaterfallSessionStorage::recentRowsFor(
    const WaterfallSessionId& id,
    qint64 fromUtcMs,
    qint64 toUtcMs) const
{
    QVector<WaterfallRow> rows;
    QMutexLocker lock(&m_mutex);
    const auto recent = m_recentRows.value(id.value);
    for (const auto& row : recent) {
        if (inRange(row.utcMs, fromUtcMs, toUtcMs)) {
            rows.push_back(row);
        }
    }
    return rows;
}

void BinaryWaterfallSessionStorage::rememberRecentRowLocked(
    const WaterfallSessionId& id,
    const WaterfallRow& row)
{
    auto& rows = m_recentRows[id.value];
    const auto insertAt = std::lower_bound(rows.begin(), rows.end(), row, rowEarlier);
    rows.insert(insertAt, row);
    while (rows.size() > kRecentRowsPerSession) {
        rows.erase(rows.begin());
    }
}

void BinaryWaterfallSessionStorage::rotateSessionsIfNeededLocked(
    const WaterfallSessionId& activeSessionId)
{
    if (m_sessions.size() <= m_config.maxSessions) {
        return;
    }

    sortSessionsLocked();
    while (m_sessions.size() > m_config.maxSessions) {
        qsizetype removeIndex = -1;
        for (qsizetype i = 0; i < m_sessions.size(); ++i) {
            if (m_sessions.at(i).id != activeSessionId && m_sessions.at(i).closed) {
                removeIndex = i;
                break;
            }
        }
        if (removeIndex < 0) {
            for (qsizetype i = 0; i < m_sessions.size(); ++i) {
                if (m_sessions.at(i).id != activeSessionId) {
                    removeIndex = i;
                    break;
                }
            }
        }
        if (removeIndex < 0) {
            break;
        }

        const auto metadata = m_sessions.at(removeIndex);
        const QString dirPath = m_sessionDirs.value(metadata.id.value);
        if (!dirPath.isEmpty() && !QDir(dirPath).removeRecursively()) {
            publishWarning(QStringLiteral("Cannot remove rotated waterfall session: %1")
                               .arg(dirPath));
            break;
        }

        m_sessionDirs.remove(metadata.id.value);
        m_recentRows.remove(metadata.id.value);
        m_sessions.removeAt(removeIndex);
    }
}

void BinaryWaterfallSessionStorage::publish(DiagnosticSeverity severity,
                                            const QString& message) const
{
    if (!m_diagnosticsSink) {
        return;
    }

    m_diagnosticsSink->publish(DiagnosticEvent{
        severity,
        "WaterfallStorage",
        message.toStdString(),
        std::chrono::system_clock::now(),
    });
}

void BinaryWaterfallSessionStorage::publishError(const QString& message) const
{
    publish(DiagnosticSeverity::Error, message);
}

void BinaryWaterfallSessionStorage::publishWarning(const QString& message) const
{
    publish(DiagnosticSeverity::Warning, message);
}

} // namespace siriusscope::infrastructure
