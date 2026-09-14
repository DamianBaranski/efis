#include "data_manager_sim.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.0f;
constexpr float kMetersPerDegLat = 111320.0f;
constexpr float kPitchRate = 25.0f * kDeg;
constexpr float kRollRate = 40.0f * kDeg;
constexpr float kYawRate = 30.0f * kDeg;
constexpr float kSpeedRate = 15.0f;
constexpr float kClimbRate = 80.0f;
}

DataManagerSim::DataManagerSim(float latitude, float longitude, float altitude)
{
    mAttitudeData = {};
    mDynamicsData.airspeed = 40.0f;
    mDynamicsData.vertical_speed = 0.0f;
    mDynamicsData.slip_rad = 0.0f;
    mEngineData = {};
    mLocationData.latitude = latitude;
    mLocationData.longitude = longitude;
    mLocationData.altitude = altitude;
}

const AttitudeData &DataManagerSim::getAttitudeData() const
{
    return mAttitudeData;
}

const DynamicsData &DataManagerSim::getDynamicsData() const
{
    return mDynamicsData;
}

const EngineData &DataManagerSim::getEngineData() const
{
    return mEngineData;
}

const LocationData &DataManagerSim::getLocationData() const
{
    return mLocationData;
}

void DataManagerSim::start()
{
    mStarted = true;
    std::cout << "Sim start position: 50.9578 N, 16.7703 E (Mirosławice EPMR), alt 800 m" << std::endl;
    publish();
}

void DataManagerSim::resetAttitude()
{
    mAttitudeData.pitch = 0.0f;
    mAttitudeData.roll = 0.0f;
    publish();
}

void DataManagerSim::tick(float dt, bool pitchUp, bool pitchDown, bool rollLeft, bool rollRight,
                          bool yawLeft, bool yawRight, bool faster, bool slower, bool climb, bool descend)
{
    if (dt <= 0.0f)
    {
        return;
    }
    dt = std::min(dt, 0.05f);

    if (pitchUp)
    {
        mAttitudeData.pitch += kPitchRate * dt;
    }
    if (pitchDown)
    {
        mAttitudeData.pitch -= kPitchRate * dt;
    }
    if (rollLeft)
    {
        mAttitudeData.roll -= kRollRate * dt;
    }
    if (rollRight)
    {
        mAttitudeData.roll += kRollRate * dt;
    }
    if (yawLeft)
    {
        mAttitudeData.heading -= kYawRate * dt;
    }
    if (yawRight)
    {
        mAttitudeData.heading += kYawRate * dt;
    }
    if (faster)
    {
        mDynamicsData.airspeed += kSpeedRate * dt;
    }
    if (slower)
    {
        mDynamicsData.airspeed -= kSpeedRate * dt;
    }
    if (climb)
    {
        mLocationData.altitude += kClimbRate * dt;
    }
    if (descend)
    {
        mLocationData.altitude -= kClimbRate * dt;
    }

    if (!rollLeft && !rollRight)
    {
        mAttitudeData.roll *= 1.0f - std::min(1.0f, 1.5f * dt);
    }
    if (!pitchUp && !pitchDown)
    {
        mAttitudeData.pitch *= 1.0f - std::min(1.0f, 0.6f * dt);
    }

    mAttitudeData.pitch = std::clamp(mAttitudeData.pitch, -70.0f * kDeg, 70.0f * kDeg);
    mAttitudeData.roll = std::clamp(mAttitudeData.roll, -80.0f * kDeg, 80.0f * kDeg);
    mDynamicsData.airspeed = std::clamp(mDynamicsData.airspeed, 0.0f, 120.0f);
    mLocationData.altitude = std::clamp(mLocationData.altitude, 50.0f, 12000.0f);

    const float heading = mAttitudeData.heading;
    const float speed = mDynamicsData.airspeed;
    const float latRad = mLocationData.latitude * kDeg;
    const float metersPerDegLon = std::max(1000.0f, kMetersPerDegLat * std::cos(latRad));
    mLocationData.latitude += (speed * std::cos(heading) * dt) / kMetersPerDegLat;
    mLocationData.longitude += (speed * std::sin(heading) * dt) / metersPerDegLon;
    mLocationData.latitude = std::clamp(mLocationData.latitude, -85.0f, 85.0f);

    mDynamicsData.vertical_speed = (climb ? kClimbRate : 0.0f) - (descend ? kClimbRate : 0.0f);

    publish();
}

void DataManagerSim::publish()
{
    notify(DataType::ATTITUDE_DATA);
    notify(DataType::LOCATION_DATA);
    notify(DataType::DYNAMICS_DATA);
}
