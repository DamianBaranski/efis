/// \file data_manager_sim.h
/// Keyboard-driven aircraft used when Stratux is not connected.
#ifndef DATA_MANAGER_SIM_H
#define DATA_MANAGER_SIM_H

#include "idata_manager.h"

/// In-process Stratux stand-in: keyboard-driven attitude and GPS.
class DataManagerSim : public IDataManager
{
public:
    /// Mirosławice ARP (EPMR): 50°57'33"N 016°46'13"E.
    static constexpr float kEpmrLatitude = 50.959167f;  ///< Degrees, north positive.
    static constexpr float kEpmrLongitude = 16.770278f; ///< Degrees, east positive.
    static constexpr float kEpmrAltitude = 800.0f;      ///< Metres above the ellipsoid.

    /// Parks the aircraft at the given WGS-84 point. Defaults to Mirosławice.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    /// \param altitude Metres above the ellipsoid.
    DataManagerSim(float latitude = kEpmrLatitude, float longitude = kEpmrLongitude,
                   float altitude = kEpmrAltitude);

    /// Pitch, roll, and heading in radians.
    const AttitudeData &getAttitudeData() const override;
    /// Airspeed and vertical speed in metres per second, slip in radians.
    const DynamicsData &getDynamicsData() const override;
    /// Engine indications. The simulator leaves these at zero.
    const EngineData &getEngineData() const override;
    /// WGS-84 position. Altitude is metres.
    const LocationData &getLocationData() const override;
    /// No background thread. The controller calls tick().
    void start() override;

    /// Steps the aircraft for dt seconds from the current key demand.
    /// \param dt Seconds since the previous step.
    void tick(float dt, bool pitchUp, bool pitchDown, bool rollLeft, bool rollRight,
              bool yawLeft, bool yawRight, bool faster, bool slower, bool climb, bool descend);

    /// Levels the wings and returns to the home point. Speed stays.
    void resetAttitude();

private:
    void publish();
    void applyHome();

    AttitudeData mAttitudeData{};
    DynamicsData mDynamicsData{};
    EngineData mEngineData{};
    LocationData mLocationData{};
    LocationData mHome{};
    bool mStarted = false;
};

#endif
