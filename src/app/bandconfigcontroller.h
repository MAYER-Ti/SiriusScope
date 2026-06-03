#pragma once

#include "bandlistmodel.h"
#include "hardware/interfaces/bco_control.h"
#include "infrastructure/interfaces/diagnostics_sink.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::app {

class BandConfigController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool editingLocked READ editingLocked WRITE setEditingLocked NOTIFY editingLockedChanged FINAL)

public:
    explicit BandConfigController(BandListModel* bandListModel,
                                  hardware::IBcoControl* bcoControl,
                                  infrastructure::IDiagnosticsSink* diagnosticsSink,
                                  QObject *parent = nullptr);
    bool editingLocked() const noexcept { return m_editingLocked; }
    void setEditingLocked(bool locked);

    Q_INVOKABLE bool applyBandSettings(int bandId,
                                       double centerHz,
                                       double widthHz,
                                       double thresholdAmplitude,
                                       int inputAttenuatorDb,
                                       int outputAttenuatorDb,
                                       const QString& polarization);

    Q_INVOKABLE bool previewBandSettings(int bandId, double centerHz, double widthHz);
    Q_INVOKABLE void cancelBandSettingsPreview(int bandId);
    Q_INVOKABLE bool setBandThresholdPreview(int bandId, double thresholdAmplitude);
    Q_INVOKABLE bool applyGeneratorPulseSettings(int bandId,
                                                 double pulsePeriodUs,
                                                 double pulseWidthUs);

signals:
    void bandSettingsApplied(int bandId);
    void bandSettingsRejected(int bandId, QString reason);
    void generatorPulseSettingsApplied(int bandId);
    void generatorPulseSettingsRejected(int bandId, QString reason);
    void bandPreviewChanged(int bandId);
    void bandPreviewCanceled(int bandId);
    void editingLockedChanged();
    void bandStateChanged(int bandId,
                          double centerHz,
                          double widthHz,
                          double thresholdDb,
                          bool enabled);

private:
    struct CommittedBandState
    {
        core::BandConfig config;
        double thresholdAmplitude = 0.0;
        int inputAttenuatorDb = 0;
        int outputAttenuatorDb = 0;
        QString polarization = QStringLiteral("horizontal");
    };

    bool initializeCommittedState();
    std::optional<CommittedBandState> committedState(int bandId) const;
    bool storeCommittedState(const CommittedBandState& state);
    std::optional<std::int64_t> frequencyFromDouble(double value, QString* reason) const;
    core::DomainResult<core::BandConfig> makeBandConfig(int bandId,
                                                        double centerHz,
                                                        double widthHz,
                                                        QString* reason) const;
    bool generatorPulseSettingsWithinBaselineBudget(int bandId,
                                                    double pulsePeriodUs,
                                                    double pulseWidthUs) const;
    bool rejectApply(int bandId, const QString& reason);
    bool rejectGeneratorPulseSettings(int bandId, const QString& reason);
    void publish(infrastructure::DiagnosticSeverity severity, const QString& message) const;
    QString validationMessage(const core::ValidationResult& validation) const;
    QString normalizedPolarization(const QString& polarization) const;
    bool isValidThreshold(double thresholdAmplitude) const;
    bool restoreCommittedState(int bandId);
    void restoreCommittedStates();
    void emitSyntheticBandState(const CommittedBandState& state);

    BandListModel* m_bandListModel = nullptr;
    hardware::IBcoControl* m_bcoControl = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    std::vector<CommittedBandState> m_committedStates;
    bool m_editingLocked = false;
};

} // namespace siriusscope::app
