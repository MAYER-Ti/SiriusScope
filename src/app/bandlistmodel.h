#pragma once

#include "core/domain_models.h"

#include <QAbstractListModel>
#include <QColor>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace siriusscope::app {

class BandListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles
    {
        BandIdRole = Qt::UserRole + 1,
        BandIndexRole,
        CenterHzRole,
        WidthHzRole,
        MinHzRole,
        MaxHzRole,
        ThresholdAmplitudeRole,
        InputAttenuatorDbRole,
        OutputAttenuatorDbRole,
        PolarizationRole,
        ColorRole,
        BorderColorRole,
        TextColorRole,
        SettingsWindowOpenRole,
        ValidRole,
        DiagnosticsRole
    };
    Q_ENUM(Roles)

    struct BandPresentationState
    {
        core::BandConfig config;
        double thresholdAmplitude = 0.0;
        int inputAttenuatorDb = 0;
        int outputAttenuatorDb = 0;
        QString polarization = QStringLiteral("horizontal");
        QColor color;
        QColor borderColor;
        QColor textColor;
        bool settingsWindowOpen = false;
        bool valid = true;
        QString diagnostics;
    };

    explicit BandListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int count() const;
    Q_INVOKABLE QVariantMap get(int index) const;
    Q_INVOKABLE int indexForBandId(int bandId) const;
    Q_INVOKABLE QVariantMap getByBandId(int bandId) const;
    Q_INVOKABLE void setSettingsWindowOpen(int bandId, bool open);

    const core::BandConfig* bandConfig(int bandId) const;
    const BandPresentationState* bandState(int bandId) const;
    const BandPresentationState* bandAt(int index) const;
    std::vector<core::BandConfig> bandConfigs() const;

    bool updateBandConfig(const core::BandConfig& config,
                          double thresholdAmplitude,
                          int inputAttenuatorDb,
                          int outputAttenuatorDb,
                          const QString& polarization,
                          bool valid = true,
                          const QString& diagnostics = QString());
    bool setThresholdAmplitude(int bandId,
                               double thresholdAmplitude,
                               bool valid = true,
                               const QString& diagnostics = QString());
    bool setBandDiagnostics(int bandId, bool valid, const QString& diagnostics);

private:
    QVariantMap toMap(const BandPresentationState& band) const;
    const BandPresentationState* stateForBandId(int bandId) const;
    BandPresentationState* stateForBandId(int bandId);
    static std::vector<BandPresentationState> makeDefaultBands();

    std::vector<BandPresentationState> m_bands;
};

} // namespace siriusscope::app
