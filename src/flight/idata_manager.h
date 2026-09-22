/// \file idata_manager.h
/// Situation source for attitude, dynamics, engine, and position.
#ifndef IDATA_MANAGER_H
#define IDATA_MANAGER_H

#include "data_type.h"
#include "subject.h"

/// Situation source for attitude, dynamics, engine, and position.
/// start() is optional. The simulator does nothing. Stratux opens the HTTP poll.
class IDataManager : public Subject<DataType> {
public:
    virtual ~IDataManager() = default;

    /// Pitch, roll, and heading in radians.
    virtual const AttitudeData &getAttitudeData() const = 0;

    /// Airspeed and vertical speed in metres per second, slip in radians.
    virtual const DynamicsData &getDynamicsData() const = 0;

    /// Engine indications. Fields stay zero when the source has no engine feed.
    virtual const EngineData &getEngineData() const = 0;

    /// WGS-84 position. Latitude and longitude in degrees, altitude in metres.
    virtual const LocationData &getLocationData() const = 0;

    /// Starts background updates. Default is a no-op.
    virtual void start() {}
};

#endif // IDATA_MANAGER_H
