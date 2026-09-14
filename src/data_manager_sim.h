#ifndef DATA_MANAGER_SIM_H
#define DATA_MANAGER_SIM_H

#include "idata_manager.h"

/// In-process Stratux stand-in: keyboard-driven attitude and GPS.
class DataManagerSim : public IDataManager
{
public:
    /// Mirosławice ARP (EPMR): 50°57'33"N 016°46'13"E, ~650 m AGL.
    static constexpr float kEpmrLatitude = 50.959167f;
    static constexpr float kEpmrLongitude = 16.770278f;
    static constexpr float kEpmrAltitude = 800.0f;

    DataManagerSim(float latitude = kEpmrLatitude, float longitude = kEpmrLongitude,
                   float altitude = kEpmrAltitude);

    const AttitudeData &getAttitudeData() const override;
    const DynamicsData &getDynamicsData() const override;
    const EngineData &getEngineData() const override;
    const LocationData &getLocationData() const override;
    void start() override;

    /// Advance physics by dt seconds using current keyboard demand.
    void tick(float dt, bool pitchUp, bool pitchDown, bool rollLeft, bool rollRight,
              bool yawLeft, bool yawRight, bool faster, bool slower, bool climb, bool descend);

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
