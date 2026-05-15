#ifndef ANTENNACONTROLLERSTUB_H
#define ANTENNACONTROLLERSTUB_H

#include "antennacontrolinterface.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

/*!
 * \class AntennaControllerStub
 * \brief QML-facing placeholder for antenna state and control commands.
 *
 * The stub is the current architectural connection point between QML and a
 * future hardware or simulator adapter. It does not format or send hardware
 * protocol messages.
 */
class AntennaControllerStub : public QObject, public AntennaControlInterface
{
    Q_OBJECT
    Q_PROPERTY(double azimuthDeg READ azimuthDeg WRITE setAzimuthDeg NOTIFY azimuthDegChanged FINAL)

public:
    explicit AntennaControllerStub(QObject *parent = nullptr);

    double azimuthDeg() const noexcept;
    void setAzimuthDeg(double azimuthDeg);

public slots:
    void stop() override;
    void driveLeft(int speed) override;
    void driveRight(int speed) override;
    void scan(double leftAngle, double rightAngle, int speed) override;

signals:
    void azimuthDegChanged(double azimuthDeg);
    void stopped();
    void driveLeftCommanded(int speed);
    void driveRightCommanded(int speed);
    void scanCommanded(double leftAngle, double rightAngle, int speed);
    void commandRejected(const QString &reason);

private:
    enum class MotionMode {
        Idle,
        DriveLeft,
        DriveRight,
        MoveToScanStart,
        ScanSector
    };

    bool validateSpeed(int speed, const char *commandName);
    bool validateAzimuth(double azimuthDeg, const char *fieldName, const char *commandName);
    bool validateSafeScanAngle(double azimuthDeg, const char *fieldName, const char *commandName);
    void startManualMotion(MotionMode mode, int speed);
    void startScanMotion(double leftCoord, double rightCoord, int speed);
    void stopMotion();
    void advanceMotion();
    void advanceDriveLeft(double elapsedSec);
    void advanceDriveRight(double elapsedSec);
    void advanceMoveToScanStart(double elapsedSec);
    void advanceScanSector(double elapsedSec);
    bool moveTowardSafeCoord(double targetCoord, double elapsedSec);
    double normalizeAzimuth(double azimuthDeg) const noexcept;
    bool isInBlindZone(double azimuthDeg) const noexcept;
    double toSafeCoord(double azimuthDeg) const noexcept;
    double fromSafeCoord(double safeCoord) const noexcept;
    double safeCoordDistance(double fromCoord, double toCoord) const noexcept;

    double m_azimuthDeg = 34.7;
    QTimer m_motionTimer;
    QElapsedTimer m_motionClock;
    MotionMode m_motionMode = MotionMode::Idle;
    double m_speedDegPerSec = 0.0;
    double m_scanStartCoord = 0.0;
    double m_scanTargetCoord = 0.0;
};

#endif // ANTENNACONTROLLERSTUB_H
