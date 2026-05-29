#pragma once

namespace siriusscope::hardware {

class IAntennaAzimuthProvider
{
public:
    virtual ~IAntennaAzimuthProvider() = default;

    virtual double currentAzimuthDeg() const = 0;
};

} // namespace siriusscope::hardware
