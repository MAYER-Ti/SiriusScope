#pragma once

#include "core/domain_models.h"

#include <QAbstractListModel>
#include <QSet>
#include <QString>

#include <vector>

namespace siriusscope::app {

class ResultTableModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role
    {
        TimeTextRole = Qt::UserRole + 1,
        ResultTimeUtcNsRole,
        AzimuthTextRole,
        BearingAzimuthDegRole,
        BandIndexRole,
        BandTextRole,
        FrequenciesTextRole,
        FrequenciesHzRole,
        QualityRole,
        QualityTextRole,
        PulseRepetitionPeriodUsRole,
        PulseRepetitionPeriodTextRole,
        PulseWidthUsRole,
        PulseWidthTextRole,
        StatusTextRole,
        DiagnosticsTextRole,
        SampleIndexRole
    };
    Q_ENUM(Role)

    explicit ResultTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const noexcept;
    Q_INVOKABLE bool containsResult(qulonglong sampleIndex,
                                    qlonglong resultTimeUtcNs,
                                    int bandIndex) const;

    bool appendRow(const core::ResultTableRow& row);
    int appendRows(const std::vector<core::ResultTableRow>& rows);
    void resetRows(std::vector<core::ResultTableRow> rows);
    const std::vector<core::ResultTableRow>& rows() const noexcept;
    bool containsRow(const core::ResultTableRow& row) const;

signals:
    void countChanged();

private:
    static QString rowKey(std::uint64_t sampleIndex, std::int64_t resultTimeUtcNs, int bandIndex);
    static QString rowKey(const core::ResultTableRow& row);

    std::vector<core::ResultTableRow> m_rows;
    QSet<QString> m_rowKeys;
};

} // namespace siriusscope::app
