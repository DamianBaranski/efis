#ifndef DATA_MANAGER_SIM_H
#define DATA_MANAGER_SIM_H

#include "idata_manager.h"
#include <chrono>

/// In-process Stratux stand-in: keyboard-driven attitude and GPS.
class DataManagerSim : public IDataManager
{
public:
    DataManagerSim(float latitude = 50.9578f, float longitude = 16.7703f, float altitude = 800.0f);

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

    AttitudeData mAttitudeData{};
    DynamicsData mDynamicsData{};
    EngineData mEngineData{};
    LocationData mLocationData{};
    bool mStarted = false;
};

#endif
