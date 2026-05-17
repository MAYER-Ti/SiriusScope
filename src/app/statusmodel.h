#pragma once

#include <QObject>
#include <QString>

class AppState;

namespace siriusscope::app {

class DiagnosticsService;
class ScanController;
class WaterfallController;

class StatusModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString programValue READ programValue NOTIFY programStatusChanged)
    Q_PROPERTY(StatusLevel programLevel READ programLevel NOTIFY programStatusChanged)
    Q_PROPERTY(QString modeValue READ modeValue NOTIFY modeStatusChanged)
    Q_PROPERTY(StatusLevel modeLevel READ modeLevel NOTIFY modeStatusChanged)
    Q_PROPERTY(QString bcoValue READ bcoValue NOTIFY bcoStatusChanged)
    Q_PROPERTY(StatusLevel bcoLevel READ bcoLevel NOTIFY bcoStatusChanged)
    Q_PROPERTY(QString bcoControlValue READ bcoControlValue NOTIFY bcoControlStatusChanged)
    Q_PROPERTY(StatusLevel bcoControlLevel READ bcoControlLevel NOTIFY bcoControlStatusChanged)
    Q_PROPERTY(QString antennaValue READ antennaValue NOTIFY antennaStatusChanged)
    Q_PROPERTY(StatusLevel antennaLevel READ antennaLevel NOTIFY antennaStatusChanged)
    Q_PROPERTY(QString azimuthValue READ azimuthValue NOTIFY azimuthChanged)
    Q_PROPERTY(StatusLevel azimuthLevel READ azimuthLevel NOTIFY azimuthChanged)
    Q_PROPERTY(QString recordingValue READ recordingValue NOTIFY recordingStatusChanged)
    Q_PROPERTY(StatusLevel recordingLevel READ recordingLevel NOTIFY recordingStatusChanged)
    Q_PROPERTY(QString diagnosticsValue READ diagnosticsValue NOTIFY diagnosticsStatusChanged)
    Q_PROPERTY(StatusLevel diagnosticsLevel READ diagnosticsLevel NOTIFY diagnosticsStatusChanged)

public:
    enum class StatusLevel : int {
        Neutral = 0,
        Good = 1,
        Warning = 2,
        Error = 3,
    };
    Q_ENUM(StatusLevel)

    explicit StatusModel(DiagnosticsService* diagnosticsService,
                         AppState* appState,
                         WaterfallController* waterfallController,
                         ScanController* scanController,
                         QObject* parent = nullptr);

    QString programValue() const { return m_programValue; }
    StatusLevel programLevel() const noexcept { return m_programLevel; }
    QString modeValue() const { return m_modeValue; }
    StatusLevel modeLevel() const noexcept { return m_modeLevel; }
    QString bcoValue() const { return m_bcoValue; }
    StatusLevel bcoLevel() const noexcept { return m_bcoLevel; }
    QString bcoControlValue() const { return m_bcoControlValue; }
    StatusLevel bcoControlLevel() const noexcept { return m_bcoControlLevel; }
    QString antennaValue() const { return m_antennaValue; }
    StatusLevel antennaLevel() const noexcept { return m_antennaLevel; }
    QString azimuthValue() const { return m_azimuthValue; }
    StatusLevel azimuthLevel() const noexcept { return m_azimuthLevel; }
    QString recordingValue() const { return m_recordingValue; }
    StatusLevel recordingLevel() const noexcept { return m_recordingLevel; }
    QString diagnosticsValue() const { return m_diagnosticsValue; }
    StatusLevel diagnosticsLevel() const noexcept { return m_diagnosticsLevel; }

signals:
    void programStatusChanged();
    void modeStatusChanged();
    void bcoStatusChanged();
    void bcoControlStatusChanged();
    void antennaStatusChanged();
    void azimuthChanged();
    void recordingStatusChanged();
    void diagnosticsStatusChanged();

private:
    void updateModeStatus();
    void updateBcoSourceStatus();
    void updateRecordingStatus();
    void updateAzimuthStatus();
    void onDiagnosticEvent(const QString& subsystem,
                           int severity,
                           const QString& message,
                           qint64 utcMs);
    void updateProgramFromDiagnostic(int severity);
    void updateDiagnosticsFromDiagnostic(int severity, const QString& message);
    void updateBcoFromDiagnostic(const QString& subsystem,
                                 int severity,
                                 const QString& message);
    void updateBcoControlFromDiagnostic(int severity, const QString& message);
    void updateAntennaFromDiagnostic(int severity, const QString& message);
    void updateStorageFromDiagnostic(int severity);
    void setProgramStatus(const QString& value, StatusLevel level);
    void setModeStatus(const QString& value, StatusLevel level);
    void setBcoStatus(const QString& value, StatusLevel level);
    void setBcoControlStatus(const QString& value, StatusLevel level);
    void setAntennaStatus(const QString& value, StatusLevel level);
    void setAzimuthStatus(const QString& value, StatusLevel level);
    void setRecordingStatus(const QString& value, StatusLevel level);
    void setDiagnosticsStatus(const QString& value, StatusLevel level);
    QString formattedAzimuth(double azimuthDeg) const;

    DiagnosticsService* m_diagnosticsService = nullptr;
    AppState* m_appState = nullptr;
    WaterfallController* m_waterfallController = nullptr;
    ScanController* m_scanController = nullptr;

    QString m_programValue = QStringLiteral("работает");
    StatusLevel m_programLevel = StatusLevel::Good;
    QString m_modeValue = QStringLiteral("генератор");
    StatusLevel m_modeLevel = StatusLevel::Good;
    QString m_bcoValue = QStringLiteral("ожидание потока");
    StatusLevel m_bcoLevel = StatusLevel::Neutral;
    QString m_bcoControlValue = QStringLiteral("ожидание");
    StatusLevel m_bcoControlLevel = StatusLevel::Neutral;
    QString m_antennaValue = QStringLiteral("ожидание");
    StatusLevel m_antennaLevel = StatusLevel::Neutral;
    QString m_azimuthValue = QStringLiteral("000,0°");
    StatusLevel m_azimuthLevel = StatusLevel::Good;
    QString m_recordingValue = QStringLiteral("выключена");
    StatusLevel m_recordingLevel = StatusLevel::Neutral;
    QString m_diagnosticsValue = QStringLiteral("ошибок нет");
    StatusLevel m_diagnosticsLevel = StatusLevel::Good;
    bool m_programHasError = false;
};

} // namespace siriusscope::app
