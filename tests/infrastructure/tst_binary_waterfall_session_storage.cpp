#include "infrastructure/storage/binary_waterfall_session_storage.h"
#include "infrastructure/storage/waterfall_storage_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

using siriusscope::infrastructure::BinaryWaterfallSessionStorage;
using siriusscope::infrastructure::DiagnosticEvent;
using siriusscope::infrastructure::IDiagnosticsSink;

class TestRunner
{
public:
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            ++m_failed;
            std::cerr << "FAILED: " << message << '\n';
        }
    }

    int result() const noexcept { return m_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE; }

private:
    int m_failed = 0;
};

class RecordingDiagnosticsSink final : public IDiagnosticsSink
{
public:
    void publish(const DiagnosticEvent& event) override
    {
        std::lock_guard lock(m_mutex);
        events.push_back(event);
    }

    mutable std::mutex m_mutex;
    std::vector<DiagnosticEvent> events;
};

QString tempDirTemplate()
{
    return QDir::current().filePath(QStringLiteral("tst_binary_waterfall_session_storage-XXXXXX"));
}

WaterfallSessionMetadata makeMetadata(QString id, qint64 startUtcMs)
{
    WaterfallSessionMetadata metadata;
    metadata.id = WaterfallSessionId{std::move(id)};
    metadata.startUtcMs = startUtcMs;
    metadata.endUtcMs = startUtcMs;
    metadata.rowPeriodMs = 1000;
    metadata.binCount = 4;
    metadata.bandCount = 5;
    metadata.beamCount = 2;
    metadata.sourceName = QStringLiteral("BCO");
    return metadata;
}

WaterfallRow makeRow(const WaterfallSessionId& id, qint64 utcMs, quint64 sampleIndex)
{
    WaterfallRow row;
    row.sessionId = id;
    row.utcMs = utcMs;
    row.firstSampleIndex = sampleIndex;
    row.lastSampleIndex = sampleIndex + 10;
    row.viewMinHz = 300'000'000.0;
    row.viewMaxHz = 800'000'000.0;
    row.bins = {
        WaterfallBeamBin{10, 20},
        WaterfallBeamBin{30, 40},
        WaterfallBeamBin{50, 60},
        WaterfallBeamBin{70, 80},
    };
    return row;
}

QString sessionDirPath(const QString& dataRootPath, const WaterfallSessionMetadata& metadata)
{
    const QString recordingsRoot =
        siriusscope::infrastructure::waterfall_storage_paths::recordingsRootPath(dataRootPath);
    return QDir(recordingsRoot).filePath(
        siriusscope::infrastructure::waterfall_storage_paths::safeSessionDirectoryName(metadata));
}

QJsonObject readMetadataJson(const QString& dataRootPath,
                             const WaterfallSessionMetadata& metadata)
{
    const QString metadataPath =
        siriusscope::infrastructure::waterfall_storage_paths::metadataPath(
            sessionDirPath(dataRootPath, metadata));
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void testCreateSession(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryWaterfallSessionStorage storage(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
        &diagnostics);

    const auto metadata = storage.startSession(makeMetadata(QStringLiteral("session-create"),
                                                            1'000));
    const QString dirPath = sessionDirPath(tempDir.path(), metadata);

    test.require(QFileInfo::exists(
                     siriusscope::infrastructure::waterfall_storage_paths::metadataPath(dirPath)),
                 "startSession creates metadata.json");
    test.require(QFileInfo::exists(
                     siriusscope::infrastructure::waterfall_storage_paths::binPath(dirPath)),
                 "startSession creates waterfall.bin");
    test.require(QFileInfo::exists(
                     siriusscope::infrastructure::waterfall_storage_paths::indexPath(dirPath)),
                 "startSession creates waterfall.idx");
    test.require(storage.listSessions().size() == 1, "listSessions returns created session");
}

void testAppendReadRow(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryWaterfallSessionStorage storage(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
        &diagnostics);

    const auto metadata = storage.startSession(makeMetadata(QStringLiteral("session-row"), 2'000));
    const auto row = makeRow(metadata.id, 2'500, 42);
    storage.appendRow(metadata.id, row);

    const auto rows = storage.loadRows(metadata.id, 2'000, 3'000, 10);
    test.require(rows.size() == 1, "loadRows returns appended row");
    if (!rows.isEmpty()) {
        test.require(rows.first().bins == row.bins, "loadRows preserves beam bins");
        test.require(rows.first().firstSampleIndex == row.firstSampleIndex,
                     "loadRows preserves first sample index");
        test.require(rows.first().lastSampleIndex == row.lastSampleIndex,
                     "loadRows preserves last sample index");
        test.require(rows.first().viewMinHz == row.viewMinHz
                         && rows.first().viewMaxHz == row.viewMaxHz,
                     "loadRows preserves source viewport");
    }
}

void testMaxRowsReturnsLatestRows(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryWaterfallSessionStorage storage(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
        &diagnostics);

    const auto metadata = storage.startSession(makeMetadata(QStringLiteral("session-max"), 3'000));
    for (int i = 0; i < 10; ++i) {
        storage.appendRow(metadata.id, makeRow(metadata.id, 3'000 + i * 1000, i));
    }

    const auto rows = storage.loadRows(metadata.id, 0, 20'000, 3);
    test.require(rows.size() == 3, "loadRows applies maxRows");
    if (rows.size() == 3) {
        test.require(rows[0].utcMs == 10'000 && rows[1].utcMs == 11'000
                         && rows[2].utcMs == 12'000,
                     "maxRows keeps latest rows in chronological order");
    }
}

void testRestartRecovery(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    WaterfallSessionMetadata metadata;

    {
        BinaryWaterfallSessionStorage storage(
            BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
            &diagnostics);
        metadata = storage.startSession(makeMetadata(QStringLiteral("session-restart"), 4'000));
        storage.appendRow(metadata.id, makeRow(metadata.id, 4'000, 1));
        storage.appendRow(metadata.id, makeRow(metadata.id, 5'000, 2));
    }

    BinaryWaterfallSessionStorage recovered(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
        &diagnostics);
    const auto latest = recovered.latestSession();
    const auto rows = recovered.loadRows(metadata.id, 0, 10'000, 10);

    test.require(latest && latest->id == metadata.id, "latestSession recovers after restart");
    test.require(rows.size() == 2, "loadRows reads rows after restart");
}

void testCloseSessionUpdatesMetadata(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryWaterfallSessionStorage storage(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
        &diagnostics);

    auto metadata = storage.startSession(makeMetadata(QStringLiteral("session-close"), 6'000));
    storage.closeSession(metadata.id, 9'000);
    metadata.endUtcMs = 9'000;
    metadata.closed = true;

    const auto json = readMetadataJson(tempDir.path(), metadata);
    test.require(json.value(QStringLiteral("closed")).toBool(false),
                 "closeSession writes closed=true");
    test.require(json.value(QStringLiteral("endUtcMs")).toVariant().toLongLong() == 9'000,
                 "closeSession writes endUtcMs");
}

void testBrokenMetadataIsIgnored(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    const QString recordingsRoot =
        siriusscope::infrastructure::waterfall_storage_paths::recordingsRootPath(tempDir.path());
    const QString brokenDir = QDir(recordingsRoot).filePath(QStringLiteral("broken"));
    QDir().mkpath(brokenDir);

    QFile file(siriusscope::infrastructure::waterfall_storage_paths::metadataPath(brokenDir));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write("{ broken json");
        file.close();
    }

    RecordingDiagnosticsSink diagnostics;
    BinaryWaterfallSessionStorage storage(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
        &diagnostics);

    test.require(storage.listSessions().isEmpty(), "broken metadata session is ignored");
}

void testMissingIndexIsRebuilt(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    WaterfallSessionMetadata metadata;

    {
        BinaryWaterfallSessionStorage storage(
            BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
            &diagnostics);
        metadata = storage.startSession(makeMetadata(QStringLiteral("session-reindex"), 7'000));
        storage.appendRow(metadata.id, makeRow(metadata.id, 7'000, 1));
    }

    const QString dirPath = sessionDirPath(tempDir.path(), metadata);
    QFile::remove(siriusscope::infrastructure::waterfall_storage_paths::indexPath(dirPath));

    BinaryWaterfallSessionStorage recovered(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 20, false},
        &diagnostics);
    const auto rows = recovered.loadRows(metadata.id, 0, 10'000, 10);

    test.require(QFileInfo::exists(
                     siriusscope::infrastructure::waterfall_storage_paths::indexPath(dirPath)),
                 "missing waterfall.idx is rebuilt on startup");
    test.require(rows.size() == 1, "rebuilt index can read saved rows");
}

void testRotationKeepsLatestSessions(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryWaterfallSessionStorage storage(
        BinaryWaterfallSessionStorage::Config{tempDir.path(), 2, false},
        &diagnostics);

    const auto first = storage.startSession(makeMetadata(QStringLiteral("session-rotate-1"),
                                                         8'000));
    storage.closeSession(first.id, 8'500);
    const auto second = storage.startSession(makeMetadata(QStringLiteral("session-rotate-2"),
                                                          9'000));
    storage.closeSession(second.id, 9'500);
    const auto third = storage.startSession(makeMetadata(QStringLiteral("session-rotate-3"),
                                                         10'000));

    const auto sessions = storage.listSessions();
    const bool hasFirst = std::any_of(sessions.cbegin(), sessions.cend(), [&](const auto& item) {
        return item.id == first.id;
    });
    const bool hasThird = std::any_of(sessions.cbegin(), sessions.cend(), [&](const auto& item) {
        return item.id == third.id;
    });

    test.require(sessions.size() == 2, "rotation keeps configured session count");
    test.require(!hasFirst, "rotation removes oldest closed session");
    test.require(hasThird, "rotation never removes active session");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testCreateSession(test);
    testAppendReadRow(test);
    testMaxRowsReturnsLatestRows(test);
    testRestartRecovery(test);
    testCloseSessionUpdatesMetadata(test);
    testBrokenMetadataIsIgnored(test);
    testMissingIndexIsRebuilt(test);
    testRotationKeepsLatestSessions(test);

    return test.result();
}
