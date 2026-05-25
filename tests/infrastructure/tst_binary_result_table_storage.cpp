#include "infrastructure/storage/binary_result_table_storage.h"
#include "infrastructure/storage/result_table_storage_format.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace {

using siriusscope::infrastructure::BinaryResultTableStorage;
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
        std::lock_guard lock(mutex);
        events.push_back(event);
        std::cerr << "DIAGNOSTIC: " << event.message << '\n';
    }

    bool contains(const std::string& text) const
    {
        std::lock_guard lock(mutex);
        for (const auto& event : events) {
            if (event.message.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    mutable std::mutex mutex;
    std::vector<DiagnosticEvent> events;
};

QString tempDirTemplate()
{
    return QDir::current().filePath(QStringLiteral("tst_binary_result_table_storage-XXXXXX"));
}

siriusscope::core::ResultTableRow makeRow(
    std::uint64_t sampleIndex = 12,
    std::int64_t utcNs = 1'700'000'000'000'000'000LL,
    int bandIndex = 1,
    std::optional<double> quality = 0.84,
    std::optional<double> pulseRepetitionPeriodUs = 100'000.0,
    std::optional<double> pulseWidthUs = 10'000.0)
{
    return siriusscope::core::ResultTableRow{
        sampleIndex,
        utcNs,
        37.0,
        46.0,
        bandIndex,
        {3'000'000'000LL, 3'100'000'000LL},
        quality,
        pulseRepetitionPeriodUs,
        pulseWidthUs,
        {siriusscope::core::ValidationIssue{
            siriusscope::core::ValidationCode::InvalidQuality,
            "diagnostic text",
        }},
    };
}

QString resultTableDir(const QTemporaryDir& tempDir)
{
    return QDir(tempDir.path()).filePath(QStringLiteral("result_table"));
}

bool nearly(double left, double right, double epsilon = 1e-9)
{
    return std::fabs(left - right) <= epsilon;
}

const siriusscope::core::ResultTableRow* findRowBySampleIndex(
    const std::vector<siriusscope::core::ResultTableRow>& rows,
    std::uint64_t sampleIndex)
{
    for (const auto& row : rows) {
        if (row.sampleIndex == sampleIndex) {
            return &row;
        }
    }
    return nullptr;
}

void writeUnsupportedVersionFile(const QString& dataRootPath, std::uint32_t formatVersion = 999)
{
    QDir().mkpath(QDir(dataRootPath).filePath(QStringLiteral("result_table")));
    QFile file(QDir(QDir(dataRootPath).filePath(QStringLiteral("result_table")))
                   .filePath(QStringLiteral("result_table.bin")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }

    siriusscope::infrastructure::result_table_storage_format::ResultTableBinFileHeader header{};
    std::memcpy(header.magic,
                siriusscope::infrastructure::result_table_storage_format::kResultTableBinMagic,
                sizeof(header.magic));
    header.formatVersion = formatVersion;
    header.headerSize = sizeof(header);
    header.byteOrder =
        siriusscope::infrastructure::result_table_storage_format::kByteOrderLittleEndian;
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
}

void testAppendCreatesFiles(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    const auto result = storage.append(makeRow());

    test.require(result.success, "append succeeds");
    test.require(QFileInfo::exists(QDir(resultTableDir(tempDir)).filePath(
                     QStringLiteral("metadata.json"))),
                 "append creates metadata.json");
    test.require(QFileInfo::exists(QDir(resultTableDir(tempDir)).filePath(
                     QStringLiteral("result_table.bin"))),
                 "append creates result_table.bin");
    test.require(QFileInfo::exists(QDir(resultTableDir(tempDir)).filePath(
                     QStringLiteral("result_table.idx"))),
                 "append creates result_table.idx");
}

void testAppendReadAllRoundTrip(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    const auto row = makeRow();
    storage.append(row);
    const auto rows = storage.readAll();

    test.require(rows.size() == 1, "readAll returns appended row");
    test.require(rows.front().sampleIndex == row.sampleIndex, "sampleIndex round-trips");
    test.require(rows.front().resultTimeUtcNs == row.resultTimeUtcNs, "time round-trips");
    test.require(rows.front().bearingAzimuthDeg == row.bearingAzimuthDeg,
                 "bearing azimuth round-trips");
    test.require(rows.front().antennaAzimuthDeg == row.antennaAzimuthDeg,
                 "antenna azimuth round-trips");
    test.require(rows.front().bandIndex == row.bandIndex, "band index round-trips");
    test.require(rows.front().frequenciesHz == row.frequenciesHz, "frequencies round-trip");
    test.require(rows.front().quality && *rows.front().quality == 0.84,
                 "quality value round-trips");
    test.require(rows.front().pulseRepetitionPeriodUs
                     && *rows.front().pulseRepetitionPeriodUs == 100'000.0,
                 "pulse repetition period round-trips");
    test.require(rows.front().pulseWidthUs && *rows.front().pulseWidthUs == 10'000.0,
                 "pulse width round-trips");
    test.require(rows.front().diagnostics.size() == 1
                     && rows.front().diagnostics.front().message == "diagnostic text",
                 "diagnostics round-trip");
}

void testMultipleRowsSortedAndQualityNull(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    const auto newer = makeRow(14, 1'700'000'010'000'000'000LL, 2, std::nullopt);
    const auto older = makeRow(13, 1'700'000'001'000'000'000LL, 1, 0.5);
    storage.append(newer);
    storage.append(older);

    const auto rows = storage.readAll();

    test.require(rows.size() == 2, "readAll returns both rows");
    test.require(rows.front().sampleIndex == 13, "readAll sorts by time");
    test.require(!rows.back().quality, "null quality round-trips");
    test.require(rows.back().pulseRepetitionPeriodUs.has_value(),
                 "signal parameters are preserved with null quality");
}

void testSignalParametersNullRoundTrip(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    const auto row = makeRow(15,
                             1'700'000'020'000'000'000LL,
                             1,
                             0.5,
                             std::nullopt,
                             std::nullopt);
    storage.append(row);
    const auto rows = storage.readAll();

    test.require(rows.size() == 1, "readAll returns row without signal parameters");
    test.require(!rows.front().pulseRepetitionPeriodUs,
                 "null pulse repetition period round-trips");
    test.require(!rows.front().pulseWidthUs, "null pulse width round-trips");
}

void testSignalParametersPresentAndMissingRoundTrip(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    const auto withParameters = makeRow(21,
                                        1'700'000'030'000'000'000LL,
                                        1,
                                        0.5,
                                        100'000.0,
                                        10'000.0);
    const auto withoutParameters = makeRow(22,
                                           1'700'000'031'000'000'000LL,
                                           2,
                                           0.5,
                                           std::nullopt,
                                           std::nullopt);

    storage.append(withParameters);
    storage.append(withoutParameters);
    const auto rows = storage.readAll();

    const auto* restoredWithParameters = findRowBySampleIndex(rows, withParameters.sampleIndex);
    const auto* restoredWithoutParameters =
        findRowBySampleIndex(rows, withoutParameters.sampleIndex);

    test.require(rows.size() == 2, "readAll returns rows with and without signal parameters");
    test.require(restoredWithParameters != nullptr, "row with signal parameters is restored");
    test.require(restoredWithParameters && restoredWithParameters->pulseRepetitionPeriodUs
                     && nearly(*restoredWithParameters->pulseRepetitionPeriodUs, 100'000.0),
                 "PRI value is restored");
    test.require(restoredWithParameters && restoredWithParameters->pulseWidthUs
                     && nearly(*restoredWithParameters->pulseWidthUs, 10'000.0),
                 "PW value is restored");
    test.require(restoredWithoutParameters != nullptr,
                 "row without signal parameters is restored");
    test.require(restoredWithoutParameters && !restoredWithoutParameters->pulseRepetitionPeriodUs,
                 "missing PRI stays nullopt");
    test.require(restoredWithoutParameters && !restoredWithoutParameters->pulseWidthUs,
                 "missing PW stays nullopt");
}

void testV1FormatRejected(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    writeUnsupportedVersionFile(tempDir.path(), 1);

    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    const auto rows = storage.readAll();

    test.require(rows.empty(), "v1 result table storage is not loaded");
    test.require(diagnostics.contains("Unsupported result_table.bin format version"),
                 "v1 result table storage is diagnosed as unsupported");
}

void testUnsupportedExistingFormatDoesNotBlockAppend(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    writeUnsupportedVersionFile(tempDir.path(), 2);

    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    const auto appendResult = storage.append(makeRow());
    const auto rows = storage.readAll();

    test.require(appendResult.success, "unsupported existing format does not block append");
    test.require(rows.size() == 1, "new row is readable after unsupported format recovery");
    test.require(diagnostics.contains("unsupported result table binary format"),
                 "unsupported existing format is diagnosed");
}

void testEmptyStorage(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    test.require(storage.readAll().empty(), "empty storage returns empty vector");
}

void testCorruptedFileDoesNotCrash(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    QDir().mkpath(resultTableDir(tempDir));
    QFile file(QDir(resultTableDir(tempDir)).filePath(QStringLiteral("result_table.bin")));
    test.require(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 "corrupted test file opens");
    file.write("bad");
    file.close();

    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    test.require(storage.readAll().empty(), "corrupted storage returns empty vector");
    test.require(diagnostics.contains("Invalid result_table.bin header"),
                 "corrupted storage is diagnosed");
}

void testUnsupportedVersionDiagnosed(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    writeUnsupportedVersionFile(tempDir.path());

    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);

    test.require(storage.readAll().empty(), "unsupported version returns empty vector");
    test.require(diagnostics.contains("Unsupported result_table.bin format version"),
                 "unsupported version is diagnosed");
}

void testPartialRecordDoesNotCrash(TestRunner& test)
{
    QTemporaryDir tempDir(tempDirTemplate());
    RecordingDiagnosticsSink diagnostics;
    BinaryResultTableStorage storage({tempDir.path(), false}, &diagnostics);
    storage.append(makeRow());

    QFile file(QDir(resultTableDir(tempDir)).filePath(QStringLiteral("result_table.bin")));
    test.require(file.open(QIODevice::ReadWrite), "partial test file opens");
    file.resize(file.size() - 5);
    file.close();

    const auto rows = storage.readAll();
    test.require(rows.empty(), "partial record is not returned");
    test.require(diagnostics.contains("Partial result table payload"),
                 "partial record is diagnosed");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testAppendCreatesFiles(test);
    testAppendReadAllRoundTrip(test);
    testMultipleRowsSortedAndQualityNull(test);
    testSignalParametersNullRoundTrip(test);
    testSignalParametersPresentAndMissingRoundTrip(test);
    testV1FormatRejected(test);
    testUnsupportedExistingFormatDoesNotBlockAppend(test);
    testEmptyStorage(test);
    testCorruptedFileDoesNotCrash(test);
    testUnsupportedVersionDiagnosed(test);
    testPartialRecordDoesNotCrash(test);

    return test.result();
}
