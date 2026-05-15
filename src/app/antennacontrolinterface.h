#ifndef ANTENNACONTROLINTERFACE_H
#define ANTENNACONTROLINTERFACE_H

/*!
 * \class AntennaControlInterface
 * \brief Application-level contract for antenna rotation commands.
 *
 * Implementations may route commands to real hardware, a simulator, or a test
 * double. UI code must depend on this command surface instead of protocol
 * details.
 */
class AntennaControlInterface
{
public:
    virtual ~AntennaControlInterface() = default;

    virtual void stop() = 0;
    virtual void driveLeft(int speed) = 0;
    virtual void driveRight(int speed) = 0;
    virtual void scan(double leftAngle, double rightAngle, int speed) = 0;
};

#endif // ANTENNACONTROLINTERFACE_H
