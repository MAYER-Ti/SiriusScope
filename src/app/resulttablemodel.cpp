#include "resulttablemodel.h"

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>
#include <QVariantList>

#include <algorithm>
#include <cmath>

namespace siriusscope::app {
namespace {

QString formatUtcNs(std::int64_t utcNs)
{
    if (utcNs < 0) {
        return {};
    }

    const auto ms = static_cast<qint64>(utcNs / 1'000'000);
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC)
        .toLocalTime()
        .toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}

QString localizedDecimal(double value, int precision, bool trimTrailingZeros)
{
    QString text = QString::number(value, 'f', precision);
    if (trimTrailingZeros) {
        while (text.contains(QLatin1Char('.')) && text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return text;
}

QString formatAzimuth(double azimuthDeg)
{
    return localizedDecimal(azimuthDeg, 1, false) + QStringLiteral("°");
}

QString formatFrequency(std::int64_t frequencyHz)
{
    return localizedDecimal(static_cast<double>(frequencyHz) / 1'000'000.0, 3, true);
}

QString formatFrequencies(const std::vector<std::int64_t>& frequenciesHz)
{
    QStringList values;
    values.reserve(static_cast<qsizetype>(frequenciesHz.size()));
    for (const auto frequencyHz : frequenciesHz) {
        values.push_back(formatFrequency(frequencyHz));
    }
    return values.join(QStringLiteral(", "));
}

QVariantList frequenciesVariant(const std::vector<std::int64_t>& frequenciesHz)
{
    QVariantList values;
    values.reserve(static_cast<qsizetype>(frequenciesHz.size()));
    for (const auto frequencyHz : frequenciesHz) {
        values.push_back(QVariant::fromValue(static_cast<qlonglong>(frequencyHz)));
    }
    return values;
}

QString validationCodeName(core::ValidationCode code)
{
    switch (code) {
    case core::ValidationCode::Ok:
        return QStringLiteral("Ok");
    case core::ValidationCode::InvalidAmplitude:
        return QStringLiteral("InvalidAmplitude");
    case core::ValidationCode::InvalidBeamIndex:
        return QStringLiteral("InvalidBeamIndex");
    case core::ValidationCode::InvalidBandIndex:
        return QStringLiteral("InvalidBandIndex");
    case core::ValidationCode::InvalidFrequency:
        return QStringLiteral("InvalidFrequency");
    case core::ValidationCode::BandOutOfRange:
        return QStringLiteral("BandOutOfRange");
    case core::ValidationCode::InvalidBandWidth:
        return QStringLiteral("InvalidBandWidth");
    case core::ValidationCode::InvalidFrequencyOffset:
        return QStringLiteral("InvalidFrequencyOffset");
    case core::ValidationCode::InvalidAzimuth:
        return QStringLiteral("InvalidAzimuth");
    case core::ValidationCode::InvalidScanSector:
        return QStringLiteral("InvalidScanSector");
    case core::ValidationCode::InvalidSampleIndex:
        return QStringLiteral("InvalidSampleIndex");
    case core::ValidationCode::InvalidTimeBase:
        return QStringLiteral("InvalidTimeBase");
    case core::ValidationCode::InvalidQuality:
        return QStringLiteral("InvalidQuality");
    case core::ValidationCode::EmptyFrequencySet:
        return QStringLiteral("EmptyFrequencySet");
    }
    return QStringLiteral("Unknown");
}

QString formatDiagnostics(const std::vector<core::ValidationIssue>& diagnostics)
{
    QStringList values;
    values.reserve(static_cast<qsizetype>(diagnostics.size()));
    for (const auto& diagnostic : diagnostics) {
        const auto message = QString::fromStdString(diagnostic.message);
        values.push_back(message.isEmpty() ? validationCodeName(diagnostic.code) : message);
    }
    return values.join(QStringLiteral("; "));
}

QString qualityText(std::optional<double> quality)
{
    if (!quality) {
        return QStringLiteral("Н/Д");
    }
    return QStringLiteral("%1%").arg(static_cast<int>(std::round(*quality * 100.0)));
}

QString formatDurationUs(std::optional<double> value)
{
    if (!value) {
        return QStringLiteral("Н/Д");
    }

    if (*value >= 1000.0) {
        return localizedDecimal(*value / 1000.0, 3, true) + QStringLiteral(" мс");
    }

    return localizedDecimal(*value, 3, true) + QStringLiteral(" мкс");
}

QString statusText(const core::ResultTableRow& row)
{
    return row.diagnostics.empty()
        ? QStringLiteral("Готово")
        : QStringLiteral("Диагностика");
}

} // namespace

ResultTableModel::ResultTableModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ResultTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(m_rows.size());
}

QVariant ResultTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }

    const auto& row = m_rows.at(static_cast<std::size_t>(index.row()));

    switch (role) {
    case TimeTextRole:
        return formatUtcNs(row.resultTimeUtcNs);
    case ResultTimeUtcNsRole:
        return QVariant::fromValue(static_cast<qlonglong>(row.resultTimeUtcNs));
    case AzimuthTextRole:
        return formatAzimuth(row.bearingAzimuthDeg);
    case BearingAzimuthDegRole:
        return row.bearingAzimuthDeg;
    case BandIndexRole:
        return row.bandIndex;
    case BandTextRole:
        return QStringLiteral("Диапазон %1").arg(row.bandIndex + 1);
    case FrequenciesTextRole:
        return formatFrequencies(row.frequenciesHz);
    case FrequenciesHzRole:
        return frequenciesVariant(row.frequenciesHz);
    case QualityRole:
        return row.quality ? *row.quality : -1.0;
    case QualityTextRole:
        return qualityText(row.quality);
    case PulseRepetitionPeriodUsRole:
        return row.pulseRepetitionPeriodUs ? *row.pulseRepetitionPeriodUs : -1.0;
    case PulseRepetitionPeriodTextRole:
        return formatDurationUs(row.pulseRepetitionPeriodUs);
    case PulseWidthUsRole:
        return row.pulseWidthUs ? *row.pulseWidthUs : -1.0;
    case PulseWidthTextRole:
        return formatDurationUs(row.pulseWidthUs);
    case StatusTextRole:
        return statusText(row);
    case DiagnosticsTextRole:
        return formatDiagnostics(row.diagnostics);
    case SampleIndexRole:
        return QVariant::fromValue(static_cast<qulonglong>(row.sampleIndex));
    default:
        return {};
    }
}

QHash<int, QByteArray> ResultTableModel::roleNames() const
{
    return {
        {TimeTextRole, QByteArrayLiteral("timeText")},
        {ResultTimeUtcNsRole, QByteArrayLiteral("resultTimeUtcNs")},
        {AzimuthTextRole, QByteArrayLiteral("azimuthText")},
        {BearingAzimuthDegRole, QByteArrayLiteral("bearingAzimuthDeg")},
        {BandIndexRole, QByteArrayLiteral("bandIndex")},
        {BandTextRole, QByteArrayLiteral("bandText")},
        {FrequenciesTextRole, QByteArrayLiteral("frequenciesText")},
        {FrequenciesHzRole, QByteArrayLiteral("frequenciesHz")},
        {QualityRole, QByteArrayLiteral("quality")},
        {QualityTextRole, QByteArrayLiteral("qualityText")},
        {PulseRepetitionPeriodUsRole, QByteArrayLiteral("pulseRepetitionPeriodUs")},
        {PulseRepetitionPeriodTextRole, QByteArrayLiteral("pulseRepetitionPeriodText")},
        {PulseWidthUsRole, QByteArrayLiteral("pulseWidthUs")},
        {PulseWidthTextRole, QByteArrayLiteral("pulseWidthText")},
        {StatusTextRole, QByteArrayLiteral("statusText")},
        {DiagnosticsTextRole, QByteArrayLiteral("diagnosticsText")},
        {SampleIndexRole, QByteArrayLiteral("sampleIndex")},
    };
}

int ResultTableModel::count() const noexcept
{
    return static_cast<int>(m_rows.size());
}

bool ResultTableModel::containsResult(qulonglong sampleIndex,
                                      qlonglong resultTimeUtcNs,
                                      int bandIndex) const
{
    return m_rowKeys.contains(rowKey(static_cast<std::uint64_t>(sampleIndex),
                                    static_cast<std::int64_t>(resultTimeUtcNs),
                                    bandIndex));
}

bool ResultTableModel::appendRow(const core::ResultTableRow& row)
{
    return appendRows({row}) == 1;
}

int ResultTableModel::appendRows(const std::vector<core::ResultTableRow>& rows)
{
    std::vector<core::ResultTableRow> uniqueRows;
    uniqueRows.reserve(rows.size());
    QSet<QString> newKeys;

    for (const auto& row : rows) {
        const auto key = rowKey(row);
        if (m_rowKeys.contains(key) || newKeys.contains(key)) {
            continue;
        }
        newKeys.insert(key);
        uniqueRows.push_back(row);
    }

    if (uniqueRows.empty()) {
        return 0;
    }

    const int firstRow = 0;
    const int lastRow = static_cast<int>(uniqueRows.size()) - 1;
    beginInsertRows(QModelIndex{}, firstRow, lastRow);
    m_rows.insert(m_rows.begin(), uniqueRows.begin(), uniqueRows.end());
    for (const auto& row : uniqueRows) {
        m_rowKeys.insert(rowKey(row));
    }
    endInsertRows();
    emit countChanged();
    return static_cast<int>(uniqueRows.size());
}

void ResultTableModel::resetRows(std::vector<core::ResultTableRow> rows)
{
    std::sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
        if (left.resultTimeUtcNs != right.resultTimeUtcNs) {
            return left.resultTimeUtcNs > right.resultTimeUtcNs;
        }
        if (left.sampleIndex != right.sampleIndex) {
            return left.sampleIndex > right.sampleIndex;
        }
        return left.bandIndex < right.bandIndex;
    });

    std::vector<core::ResultTableRow> uniqueRows;
    uniqueRows.reserve(rows.size());
    QSet<QString> uniqueKeys;
    for (const auto& row : rows) {
        const auto key = rowKey(row);
        if (uniqueKeys.contains(key)) {
            continue;
        }
        uniqueKeys.insert(key);
        uniqueRows.push_back(row);
    }

    beginResetModel();
    m_rows = std::move(uniqueRows);
    m_rowKeys = std::move(uniqueKeys);
    endResetModel();
    emit countChanged();
}

const std::vector<core::ResultTableRow>& ResultTableModel::rows() const noexcept
{
    return m_rows;
}

bool ResultTableModel::containsRow(const core::ResultTableRow& row) const
{
    return m_rowKeys.contains(rowKey(row));
}

QString ResultTableModel::rowKey(std::uint64_t sampleIndex,
                                 std::int64_t resultTimeUtcNs,
                                 int bandIndex)
{
    return QStringLiteral("%1:%2:%3")
        .arg(static_cast<qulonglong>(sampleIndex))
        .arg(static_cast<qlonglong>(resultTimeUtcNs))
        .arg(bandIndex);
}

QString ResultTableModel::rowKey(const core::ResultTableRow& row)
{
    return rowKey(row.sampleIndex, row.resultTimeUtcNs, row.bandIndex);
}

} // namespace siriusscope::app
