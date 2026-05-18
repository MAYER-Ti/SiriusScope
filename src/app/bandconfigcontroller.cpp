#include "bandconfigcontroller.h"

#include <QDebug>
#include <QStringList>

#include <chrono>
#include <cmath>
#include <limits>
#include <string>

namespace siriusscope::app {

namespace {

constexpr double kMinThresholdAmplitude = 1.0;
constexpr double kMaxThresholdAmplitude = 127.0;

} // namespace

BandConfigController::BandConfigController(BandListModel* bandListModel,
                                           hardware::IBcoControl* bcoControl,
                                           infrastructure::IDiagnosticsSink* diagnosticsSink,
                                           QObject *parent)
    : QObject(parent)
    , m_bandListModel(bandListModel)
    , m_bcoControl(bcoControl)
    , m_diagnosticsSink(diagnosticsSink)
{
    initializeCommittedState();
}

void BandConfigController::setEditingLocked(bool locked)
{
    if (m_editingLocked == locked) {
        return;
    }

    m_editingLocked = locked;
    if (m_editingLocked) {
        restoreCommittedStates();
    }
    emit editingLockedChanged();
}

bool BandConfigController::applyBandSettings(int bandId,
                                             double centerHz,
                                             double widthHz,
                                             double thresholdAmplitude,
                                             int inputAttenuatorDb,
                                             int outputAttenuatorDb,
                                             const QString& polarization)
{
    if (m_editingLocked) {
        return rejectApply(bandId, QStringLiteral("Запись включена"));
    }
    if (!m_bandListModel) {
        return rejectApply(bandId, QStringLiteral("Band model is not available"));
    }
    if (!isValidThreshold(thresholdAmplitude)) {
        return rejectApply(bandId,
                           QStringLiteral("threshold amplitude must be in range 1..127"));
    }

    const QString normalized = normalizedPolarization(polarization);
    if (normalized.isEmpty()) {
        return rejectApply(bandId,
                           QStringLiteral("polarization must be horizontal or vertical"));
    }

    QString reason;
    const auto createdConfig = makeBandConfig(bandId, centerHz, widthHz, &reason);
    if (!createdConfig) {
        return rejectApply(bandId, reason);
    }

    const auto previousState = committedState(bandId);
    CommittedBandState nextState{*createdConfig.value(),
                                 thresholdAmplitude,
                                 inputAttenuatorDb,
                                 outputAttenuatorDb,
                                 normalized};

    m_bandListModel->updateBandConfig(nextState.config,
                                      nextState.thresholdAmplitude,
                                      nextState.inputAttenuatorDb,
                                      nextState.outputAttenuatorDb,
                                      nextState.polarization);

    if (m_bcoControl) {
        const auto hardwareResult = m_bcoControl->applyBandConfig(nextState.config);
        if (!hardwareResult) {
            if (previousState) {
                m_bandListModel->updateBandConfig(previousState->config,
                                                  previousState->thresholdAmplitude,
                                                  previousState->inputAttenuatorDb,
                                                  previousState->outputAttenuatorDb,
                                                  previousState->polarization);
                emitSyntheticBandState(*previousState);
            }

            const QString hardwareReason =
                QStringLiteral("Band %1 settings rejected by BCO control: %2")
                    .arg(bandId + 1)
                    .arg(QString::fromStdString(hardwareResult.message));
            m_bandListModel->setBandDiagnostics(bandId, false, hardwareReason);
            publish(infrastructure::DiagnosticSeverity::Error, hardwareReason);
            emit bandSettingsRejected(bandId, hardwareReason);
            return false;
        }
    }

    storeCommittedState(nextState);
    m_bandListModel->setBandDiagnostics(bandId, true, QString());

    const QString message = QStringLiteral("Band %1 settings applied: center=%2 MHz, width=%3 MHz")
                                .arg(bandId + 1)
                                .arg(nextState.config.centerFrequencyHz / 1'000'000.0, 0, 'f', 3)
                                .arg(nextState.config.widthHz / 1'000'000.0, 0, 'f', 3);
    publish(infrastructure::DiagnosticSeverity::Info, message);

    emitSyntheticBandState(nextState);
    emit bandSettingsApplied(bandId);
    return true;
}

bool BandConfigController::previewBandSettings(int bandId, double centerHz, double widthHz)
{
    if (m_editingLocked) {
        return false;
    }
    if (!m_bandListModel) {
        return false;
    }

    QString reason;
    const auto createdConfig = makeBandConfig(bandId, centerHz, widthHz, &reason);
    if (!createdConfig) {
        m_bandListModel->setBandDiagnostics(bandId, false, reason);
        publish(infrastructure::DiagnosticSeverity::Warning,
                QStringLiteral("Band %1 preview rejected: %2").arg(bandId + 1).arg(reason));
        return false;
    }

    const auto* current = m_bandListModel->bandState(bandId);
    if (!current) {
        return false;
    }

    m_bandListModel->updateBandConfig(*createdConfig.value(),
                                      current->thresholdAmplitude,
                                      current->inputAttenuatorDb,
                                      current->outputAttenuatorDb,
                                      current->polarization);

    CommittedBandState previewState{*createdConfig.value(),
                                    current->thresholdAmplitude,
                                    current->inputAttenuatorDb,
                                    current->outputAttenuatorDb,
                                    current->polarization};
    emitSyntheticBandState(previewState);
    emit bandPreviewChanged(bandId);
    return true;
}

void BandConfigController::cancelBandSettingsPreview(int bandId)
{
    if (!restoreCommittedState(bandId)) {
        return;
    }

    const QString message = QStringLiteral("Band %1 preview canceled").arg(bandId + 1);
    publish(infrastructure::DiagnosticSeverity::Info, message);
    emit bandPreviewCanceled(bandId);
}

bool BandConfigController::setBandThresholdPreview(int bandId, double thresholdAmplitude)
{
    if (m_editingLocked) {
        return false;
    }
    if (!m_bandListModel || !isValidThreshold(thresholdAmplitude)) {
        return false;
    }

    const auto* current = m_bandListModel->bandState(bandId);
    if (!current) {
        return false;
    }

    m_bandListModel->setThresholdAmplitude(bandId, thresholdAmplitude);
    emit bandStateChanged(bandId,
                          current->config.centerFrequencyHz,
                          current->config.widthHz,
                          thresholdAmplitude,
                          current->config.enabled);
    emit bandPreviewChanged(bandId);
    return true;
}

bool BandConfigController::initializeCommittedState()
{
    if (!m_bandListModel) {
        return false;
    }

    m_committedStates.clear();
    m_committedStates.reserve(static_cast<std::size_t>(m_bandListModel->count()));

    for (int row = 0; row < m_bandListModel->count(); ++row) {
        const auto* state = m_bandListModel->bandAt(row);
        if (!state) {
            continue;
        }
        m_committedStates.push_back(CommittedBandState{state->config,
                                                       state->thresholdAmplitude,
                                                       state->inputAttenuatorDb,
                                                       state->outputAttenuatorDb,
                                                       state->polarization});
    }

    return static_cast<int>(m_committedStates.size()) == m_bandListModel->count();
}

std::optional<BandConfigController::CommittedBandState> BandConfigController::committedState(
    int bandId) const
{
    for (const auto& state : m_committedStates) {
        if (state.config.bandIndex == bandId) {
            return state;
        }
    }

    return std::nullopt;
}

bool BandConfigController::storeCommittedState(const CommittedBandState& state)
{
    for (auto& committed : m_committedStates) {
        if (committed.config.bandIndex == state.config.bandIndex) {
            committed = state;
            return true;
        }
    }

    m_committedStates.push_back(state);
    return true;
}

std::optional<std::int64_t> BandConfigController::frequencyFromDouble(double value,
                                                                      QString* reason) const
{
    if (!std::isfinite(value)) {
        if (reason) {
            *reason = QStringLiteral("frequency value must be finite");
        }
        return std::nullopt;
    }

    constexpr double maxInt64 = static_cast<double>(std::numeric_limits<std::int64_t>::max());
    constexpr double minInt64 = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    if (value < minInt64 || value > maxInt64) {
        if (reason) {
            *reason = QStringLiteral("frequency value is outside int64 range");
        }
        return std::nullopt;
    }

    return static_cast<std::int64_t>(std::llround(value));
}

core::DomainResult<core::BandConfig> BandConfigController::makeBandConfig(int bandId,
                                                                          double centerHz,
                                                                          double widthHz,
                                                                          QString* reason) const
{
    const auto center = frequencyFromDouble(centerHz, reason);
    if (!center) {
        return core::DomainResult<core::BandConfig>::failure(
            core::ValidationResult::invalid(core::ValidationCode::InvalidFrequency,
                                            reason ? reason->toStdString() : std::string{}));
    }

    const auto width = frequencyFromDouble(widthHz, reason);
    if (!width) {
        return core::DomainResult<core::BandConfig>::failure(
            core::ValidationResult::invalid(core::ValidationCode::InvalidBandWidth,
                                            reason ? reason->toStdString() : std::string{}));
    }

    const auto created = core::BandConfig::create(bandId, *center, *width);
    if (!created && reason) {
        *reason = validationMessage(created.validation());
    }

    return created;
}

bool BandConfigController::rejectApply(int bandId, const QString& reason)
{
    if (m_bandListModel) {
        m_bandListModel->setBandDiagnostics(bandId, false, reason);
    }

    const QString message =
        QStringLiteral("Band %1 settings rejected: %2").arg(bandId + 1).arg(reason);
    publish(infrastructure::DiagnosticSeverity::Warning, message);
    emit bandSettingsRejected(bandId, reason);
    return false;
}

void BandConfigController::publish(infrastructure::DiagnosticSeverity severity,
                                   const QString& message) const
{
    if (!m_diagnosticsSink) {
        if (severity == infrastructure::DiagnosticSeverity::Error) {
            qWarning().noquote() << message;
        } else {
            qInfo().noquote() << message;
        }
        return;
    }

    m_diagnosticsSink->publish(infrastructure::DiagnosticEvent{
        severity,
        "BandConfig",
        message.toStdString(),
        std::chrono::system_clock::now(),
    });
}

QString BandConfigController::validationMessage(const core::ValidationResult& validation) const
{
    QStringList messages;
    for (const auto& issue : validation.issues()) {
        if (!issue.message.empty()) {
            messages.push_back(QString::fromStdString(issue.message));
        } else {
            messages.push_back(QStringLiteral("validation error %1")
                                   .arg(static_cast<int>(issue.code)));
        }
    }

    return messages.isEmpty() ? QStringLiteral("invalid band configuration")
                              : messages.join(QStringLiteral("; "));
}

QString BandConfigController::normalizedPolarization(const QString& polarization) const
{
    const QString normalized = polarization.trimmed().toLower();
    if (normalized == QStringLiteral("horizontal") || normalized == QStringLiteral("vertical")) {
        return normalized;
    }

    return {};
}

bool BandConfigController::isValidThreshold(double thresholdAmplitude) const
{
    return std::isfinite(thresholdAmplitude) && thresholdAmplitude >= kMinThresholdAmplitude
        && thresholdAmplitude <= kMaxThresholdAmplitude;
}

bool BandConfigController::restoreCommittedState(int bandId)
{
    if (!m_bandListModel) {
        return false;
    }

    const auto state = committedState(bandId);
    if (!state) {
        return false;
    }

    const bool restored = m_bandListModel->updateBandConfig(state->config,
                                                            state->thresholdAmplitude,
                                                            state->inputAttenuatorDb,
                                                            state->outputAttenuatorDb,
                                                            state->polarization);
    if (restored) {
        emitSyntheticBandState(*state);
    }

    return restored;
}

void BandConfigController::restoreCommittedStates()
{
    for (const auto& state : m_committedStates) {
        if (!m_bandListModel) {
            return;
        }

        const bool restored = m_bandListModel->updateBandConfig(state.config,
                                                                state.thresholdAmplitude,
                                                                state.inputAttenuatorDb,
                                                                state.outputAttenuatorDb,
                                                                state.polarization);
        if (restored) {
            emitSyntheticBandState(state);
        }
    }
}

void BandConfigController::emitSyntheticBandState(const CommittedBandState& state)
{
    emit bandStateChanged(state.config.bandIndex,
                          state.config.centerFrequencyHz,
                          state.config.widthHz,
                          state.thresholdAmplitude,
                          state.config.enabled);
}

} // namespace siriusscope::app
