#include "app/resulttablemodel.h"

#include <QCoreApplication>
#include <QVariantList>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using siriusscope::app::ResultTableModel;

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

siriusscope::core::ResultTableRow makeRow(std::uint64_t sampleIndex = 12,
                                          std::int64_t utcNs = 1'700'000'000'000'000'000LL,
                                          int bandIndex = 1)
{
    return siriusscope::core::ResultTableRow{
        sampleIndex,
        utcNs,
        37.0,
        46.0,
        bandIndex,
        {3'000'000'000LL, 3'100'000'000LL},
        0.84,
        100'000.0,
        10'000.0,
        {},
    };
}

QVariant dataAt(ResultTableModel& model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

void testEmptyModel(TestRunner& test)
{
    ResultTableModel model;
    test.require(model.rowCount() == 0, "empty model has no rows");
    test.require(model.count() == 0, "count property starts at zero");
}

void testRoleNames(TestRunner& test)
{
    ResultTableModel model;
    const auto roles = model.roleNames();
    const std::vector<QByteArray> expected = {
        "timeText",
        "resultTimeUtcNs",
        "azimuthText",
        "bearingAzimuthDeg",
        "bandIndex",
        "bandText",
        "frequenciesText",
        "frequenciesHz",
        "quality",
        "qualityText",
        "pulseRepetitionPeriodUs",
        "pulseRepetitionPeriodText",
        "pulseWidthUs",
        "pulseWidthText",
        "statusText",
        "diagnosticsText",
        "sampleIndex",
    };

    for (const auto& roleName : expected) {
        test.require(roles.values().contains(roleName),
                     "roleNames contains " + roleName.toStdString());
    }
}

void testAppendAndData(TestRunner& test)
{
    ResultTableModel model;
    const auto row = makeRow();

    test.require(model.appendRow(row), "appendRow accepts first row");
    test.require(model.count() == 1, "appendRow increments count");
    test.require(!dataAt(model, 0, ResultTableModel::TimeTextRole).toString().isEmpty(),
                 "timeText is formatted");
    test.require(dataAt(model, 0, ResultTableModel::ResultTimeUtcNsRole).toLongLong()
                     == row.resultTimeUtcNs,
                 "resultTimeUtcNs is preserved");
    test.require(dataAt(model, 0, ResultTableModel::AzimuthTextRole).toString()
                     == QStringLiteral("37,0°"),
                 "azimuthText uses bearing azimuth with one decimal");
    test.require(dataAt(model, 0, ResultTableModel::BearingAzimuthDegRole).toDouble()
                     == row.bearingAzimuthDeg,
                 "bearingAzimuthDeg role returns numeric bearing azimuth");
    test.require(dataAt(model, 0, ResultTableModel::BandIndexRole).toInt() == 1,
                 "bandIndex is preserved");
    test.require(dataAt(model, 0, ResultTableModel::BandTextRole).toString()
                     == QStringLiteral("Диапазон 2"),
                 "bandText is one-based");
    test.require(dataAt(model, 0, ResultTableModel::FrequenciesTextRole).toString()
                     == QStringLiteral("3000, 3100"),
                 "frequenciesText is rendered in MHz");
    test.require(dataAt(model, 0, ResultTableModel::FrequenciesHzRole).toList().size() == 2,
                 "frequenciesHz returns original values");
    test.require(dataAt(model, 0, ResultTableModel::QualityRole).toDouble() == 0.84,
                 "quality role returns numeric quality");
    test.require(dataAt(model, 0, ResultTableModel::QualityTextRole).toString()
                     == QStringLiteral("84%"),
                 "qualityText is formatted as percent");
    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodUsRole).toDouble()
                     == 100'000.0,
                 "pulse repetition period role returns numeric microseconds");
    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodTextRole).toString()
                     == QStringLiteral("100 мс"),
                 "pulse repetition period text is formatted");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthUsRole).toDouble()
                     == 10'000.0,
                 "pulse width role returns numeric microseconds");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("10 мс"),
                 "pulse width text is formatted");
    test.require(dataAt(model, 0, ResultTableModel::StatusTextRole).toString()
                     == QStringLiteral("Готово"),
                 "statusText is ready without diagnostics");
    test.require(dataAt(model, 0, ResultTableModel::SampleIndexRole).toULongLong()
                     == row.sampleIndex,
                 "sampleIndex is preserved");
}

void testAppendInsertsNewRowsAtTop(TestRunner& test)
{
    ResultTableModel model;
    const auto older = makeRow(12, 1'700'000'000'000'000'000LL, 1);
    const auto newer = makeRow(13, 1'700'000'001'000'000'000LL, 1);

    test.require(model.appendRow(older), "older row is appended");
    test.require(model.appendRow(newer), "newer row is appended");

    test.require(dataAt(model, 0, ResultTableModel::SampleIndexRole).toULongLong() == 13,
                 "newly appended row is inserted at the top");
    test.require(dataAt(model, 1, ResultTableModel::SampleIndexRole).toULongLong() == 12,
                 "previous row moves down after append");
}

void testResetAndDeduplication(TestRunner& test)
{
    ResultTableModel model;
    const auto first = makeRow(12, 1'700'000'000'000'000'000LL, 1);
    const auto duplicate = first;
    const auto second = makeRow(13, 1'700'000'001'000'000'000LL, 2);

    model.resetRows({second, duplicate, first});

    test.require(model.count() == 2, "resetRows removes duplicates");
    test.require(dataAt(model, 0, ResultTableModel::SampleIndexRole).toULongLong() == 13,
                 "resetRows sorts newest rows first");
    test.require(dataAt(model, 1, ResultTableModel::SampleIndexRole).toULongLong() == 12,
                 "resetRows keeps older rows below newer rows");
    test.require(!model.appendRow(first), "appendRow rejects duplicate row");
    test.require(model.count() == 2, "duplicate append does not change count");
    test.require(model.containsRow(second), "containsRow finds existing row");
}

void testQualityAndDiagnosticsText(TestRunner& test)
{
    ResultTableModel model;
    auto row = makeRow();
    row.quality = std::nullopt;
    row.pulseRepetitionPeriodUs = std::nullopt;
    row.pulseWidthUs = std::nullopt;
    row.diagnostics.push_back(siriusscope::core::ValidationIssue{
        siriusscope::core::ValidationCode::InvalidQuality,
        "quality estimate missing",
    });

    model.appendRow(row);

    test.require(dataAt(model, 0, ResultTableModel::QualityRole).toDouble() == -1.0,
                 "missing quality returns -1");
    test.require(dataAt(model, 0, ResultTableModel::QualityTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "missing quality text is rendered");
    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodUsRole).toDouble()
                     == -1.0,
                 "missing pulse repetition period returns -1");
    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "missing pulse repetition period text is rendered");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthUsRole).toDouble() == -1.0,
                 "missing pulse width returns -1");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "missing pulse width text is rendered");
    test.require(dataAt(model, 0, ResultTableModel::StatusTextRole).toString()
                     == QStringLiteral("Диагностика"),
                 "diagnostics change status text");
    test.require(dataAt(model, 0, ResultTableModel::DiagnosticsTextRole).toString()
                     == QStringLiteral("quality estimate missing"),
                 "diagnosticsText includes message");
}

void testMissingPulseParametersText(TestRunner& test)
{
    ResultTableModel model;
    auto row = makeRow();
    row.pulseRepetitionPeriodUs = std::nullopt;
    row.pulseWidthUs = std::nullopt;

    model.appendRow(row);

    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodUsRole).toDouble()
                     == -1.0,
                 "missing PRI numeric role returns -1");
    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "missing PRI text is N/D");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthUsRole).toDouble() == -1.0,
                 "missing PW numeric role returns -1");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "missing PW text is N/D");
}

void testMicrosecondPulseParametersText(TestRunner& test)
{
    ResultTableModel model;
    auto row = makeRow();
    row.pulseRepetitionPeriodUs = 320.0;
    row.pulseWidthUs = 640.0;

    model.appendRow(row);

    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodTextRole).toString()
                     == QStringLiteral("320 мкс"),
                 "PRI under one millisecond is formatted in microseconds");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("640 мкс"),
                 "PW under one millisecond is formatted in microseconds");
}

void testMillisecondPulseParametersText(TestRunner& test)
{
    ResultTableModel model;
    auto row = makeRow();
    row.pulseRepetitionPeriodUs = 100'000.0;
    row.pulseWidthUs = 10'000.0;

    model.appendRow(row);

    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodTextRole).toString()
                     == QStringLiteral("100 мс"),
                 "PRI at or above one millisecond is formatted in milliseconds");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("10 мс"),
                 "PW at or above one millisecond is formatted in milliseconds");

    auto fractionalRow = makeRow(13, 1'700'000'001'000'000'000LL, 1);
    fractionalRow.pulseRepetitionPeriodUs = 100'000.125;
    fractionalRow.pulseWidthUs = 1'000.0;
    model.appendRow(fractionalRow);

    test.require(dataAt(model, 0, ResultTableModel::PulseRepetitionPeriodTextRole).toString()
                     == QStringLiteral("100 мс"),
                 "fractional milliseconds trim insignificant zero decimals");
    test.require(dataAt(model, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("1 мс"),
                 "one millisecond is formatted in milliseconds");
}

void testInvalidPulseParametersText(TestRunner& test)
{
    ResultTableModel zeroAndNegativeModel;
    auto zeroAndNegativeRow = makeRow();
    zeroAndNegativeRow.pulseRepetitionPeriodUs = 0.0;
    zeroAndNegativeRow.pulseWidthUs = -1.0;

    zeroAndNegativeModel.appendRow(zeroAndNegativeRow);

    test.require(dataAt(zeroAndNegativeModel, 0, ResultTableModel::PulseRepetitionPeriodUsRole)
                     .toDouble()
                     == -1.0,
                 "zero PRI numeric role returns -1");
    test.require(dataAt(zeroAndNegativeModel, 0, ResultTableModel::PulseRepetitionPeriodTextRole)
                     .toString()
                     == QStringLiteral("Н/Д"),
                 "zero PRI text is N/D");
    test.require(dataAt(zeroAndNegativeModel, 0, ResultTableModel::PulseWidthUsRole).toDouble()
                     == -1.0,
                 "negative PW numeric role returns -1");
    test.require(dataAt(zeroAndNegativeModel, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "negative PW text is N/D");

    ResultTableModel nanModel;
    auto nanRow = makeRow();
    nanRow.pulseRepetitionPeriodUs = std::numeric_limits<double>::quiet_NaN();
    nanRow.pulseWidthUs = std::numeric_limits<double>::quiet_NaN();

    nanModel.appendRow(nanRow);

    test.require(dataAt(nanModel, 0, ResultTableModel::PulseRepetitionPeriodUsRole).toDouble()
                     == -1.0,
                 "NaN PRI numeric role returns -1");
    test.require(dataAt(nanModel, 0, ResultTableModel::PulseRepetitionPeriodTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "NaN PRI text is N/D");
    test.require(dataAt(nanModel, 0, ResultTableModel::PulseWidthUsRole).toDouble() == -1.0,
                 "NaN PW numeric role returns -1");
    test.require(dataAt(nanModel, 0, ResultTableModel::PulseWidthTextRole).toString()
                     == QStringLiteral("Н/Д"),
                 "NaN PW text is N/D");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    TestRunner test;

    testEmptyModel(test);
    testRoleNames(test);
    testAppendAndData(test);
    testAppendInsertsNewRowsAtTop(test);
    testResetAndDeduplication(test);
    testQualityAndDiagnosticsText(test);
    testMissingPulseParametersText(test);
    testMicrosecondPulseParametersText(test);
    testMillisecondPulseParametersText(test);
    testInvalidPulseParametersText(test);

    return test.result();
}
