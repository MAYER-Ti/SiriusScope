#pragma once

#include "core/antenna_motion_planner.h"
#include "hardware/interfaces/antenna_azimuth_source.h"
#include "hardware/interfaces/antenna_control.h"
#include "infrastructure/interfaces/diagnostics_sink.h"
#include "processing/sample_processor.h"

#include <QObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace siriusscope::infrastructure {
class IDiagnosticsSink;
}

namespace siriusscope::app {

class BearingFrameBus;

class ScanController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double currentAzimuthDeg READ currentAzimuthDeg NOTIFY currentAzimuthChanged)
    Q_PROPERTY(bool hasSelectedSector READ hasSelectedSector NOTIFY selectedSectorChanged)
    Q_PROPERTY(double selectedLeftAngle READ selectedLeftAngle NOTIFY selectedSectorChanged)
    Q_PROPERTY(double selectedRightAngle READ selectedRightAngle NOTIFY selectedSectorChanged)
    Q_PROPERTY(bool scanActive READ scanActive NOTIFY scanStateChanged)
    Q_PROPERTY(QString scanStateText READ scanStateText NOTIFY scanStateChanged)
    Q_PROPERTY(double scanProgress READ scanProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY scanStateChanged)
    Q_PROPERTY(double scanSpeedDegPerSec READ scanSpeedDegPerSec WRITE setScanSpeedDegPerSec
                   NOTIFY scanSpeedChanged)

public:
    enum class ScanState {
        Idle,
        SectorSelected,
        MovingToStart,
        Scanning,
        Completing,
        Completed,
        Cancelled,
        Failed,
    };
    Q_ENUM(ScanState)

    explicit ScanController(hardware::IAntennaControl* antennaControl,
                            hardware::IAntennaAzimuthSource* azimuthSource,
                            BearingFrameBus* bearingFrameBus,
                            infrastructure::IDiagnosticsSink* diagnosticsSink,
                            QObject* parent = nullptr);
    ~ScanController() override;

    double currentAzimuthDeg() const noexcept { return m_currentAzimuthDeg; }
    bool hasSelectedSector() const noexcept { return m_selectedSector.has_value(); }
    double selectedLeftAngle() const noexcept;
    double selectedRightAngle() const noexcept;
    bool scanActive() const noexcept;
    QString scanStateText() const;
    double scanProgress() const noexcept { return m_scanProgress; }
    QString lastError() const { return m_lastError; }
    double scanSpeedDegPerSec() const noexcept { return m_scanSpeedDegPerSec; }

    Q_INVOKABLE void selectSector(double leftAngleDeg, double rightAngleDeg);
    Q_INVOKABLE void clearSector();
    Q_INVOKABLE void startSelectedSectorScan(double speedDegPerSec);
    Q_INVOKABLE void startScan(double leftAngleDeg, double rightAngleDeg, double speedDegPerSec);
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void driveLeft(double speedDegPerSec);
    Q_INVOKABLE void driveRight(double speedDegPerSec);
    Q_INVOKABLE void setScanSpeedDegPerSec(double speedDegPerSec);

signals:
    void currentAzimuthChanged();
    void selectedSectorChanged();
    void scanStateChanged();
    void scanProgressChanged();
    void scanSpeedChanged();
    void scanCompleted(qulonglong sessionId, int frameCount);
    void scanCancelled(qulonglong sessionId);
    void scanFailed(qulonglong sessionId, QString reason);

private:
    struct ScanSession
    {
        std::uint64_t id = 0;
        core::ScanSector requestedSector;
        core::PlannedScanPath plannedPath;
        std::chrono::system_clock::time_point startedAt;
        std::chrono::system_clock::time_point finishedAt;
        double startAzimuthDeg = 0.0;
        double lastAzimuthDeg = 0.0;
        double progress = 0.0;
        double speedDegPerSec = 10.0;
        std::vector<processing::BearingInputFrame> collectedBearingFrames;
    };

    void startAzimuthSource();
    void handleAzimuthSample(const hardware::AntennaAzimuthSample& sample);
    void updateAzimuth(const hardware::AntennaAzimuthSample& sample);
    void beginSectorScan();
    void completeScan();
    void failScan(const QString& reason);
    void onBearingFrames(std::vector<processing::BearingInputFrame> frames);
    void startManualMove(hardware::AntennaManualMoveCommand::Direction direction,
                         double speedDegPerSec);
    void setState(ScanState state);
    void setProgress(double progress);
    void setLastError(QString error);
    void storeSelectedSector(const core::ScanSector& sector);
    core::ScanMotionOptions scanOptions(double speedDegPerSec) const;
    QString validationMessage(const core::ValidationResult& validation) const;
    bool validateSpeed(double speedDegPerSec, QString* error) const;
    void publish(infrastructure::DiagnosticSeverity severity, const std::string& message) const;

    hardware::IAntennaControl* m_antennaControl = nullptr;
    hardware::IAntennaAzimuthSource* m_azimuthSource = nullptr;
    BearingFrameBus* m_bearingFrameBus = nullptr;
    infrastructure::IDiagnosticsSink* m_diagnosticsSink = nullptr;
    int m_bearingFrameSubscriptionId = 0;

    double m_currentAzimuthDeg = 0.0;
    std::chrono::system_clock::time_point m_lastAzimuthTimestamp;
    std::optional<core::ScanSector> m_selectedSector;
    std::optional<ScanSession> m_activeSession;
    ScanState m_state = ScanState::Idle;
    double m_scanProgress = 0.0;
    QString m_lastError;
    double m_scanSpeedDegPerSec = 10.0;
    std::uint64_t m_nextSessionId = 1;
};

} // namespace siriusscope::app
