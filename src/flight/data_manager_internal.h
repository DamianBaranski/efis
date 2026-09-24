/// \file data_manager_internal.h
/// Attitude and position from the tablet GPS, gyro, accelerometer, and compass.

#ifndef DATA_MANAGER_INTERNAL_H
#define DATA_MANAGER_INTERNAL_H

#include "idata_manager.h"
#include "session.h"

#include <cstdint>

/// Tablet sensors. Pitch and roll are integrated from the gyro. Heading comes from the compass.
/// Latitude, longitude, altitude, and ground speed come from GPS.
/// The desktop build keeps the seeded sample. The sensors exist on the tablet.
class DataManagerInternal : public IDataManager
{
public:
    /// Pitch, roll, and heading in radians. Heading is clockwise from magnetic north.
    const AttitudeData &getAttitudeData() const override;
    /// Ground speed and vertical speed in metres per second. Slip stays zero.
    const DynamicsData &getDynamicsData() const override;
    /// Engine indications. The tablet has no engine feed.
    const EngineData &getEngineData() const override;
    /// WGS-84 position. Altitude is metres above the ellipsoid.
    const LocationData &getLocationData() const override;

    /// Copies the sample shown until the first GPS and attitude fix.
    void seed(const AttitudeData &attitude, const DynamicsData &dynamics, const LocationData &location);

    /// Starts or stops the tablet listeners. On the desktop this only logs.
    void setRunning(bool on);

    /// Reads the latest sensor sample and publishes it.
    void tick();

    /// GPS, gyro, accelerometer, and compass, for the SOURCES page.
    const SensorReport &report() const { return mReport; }

    /// Stores the attitude being held as the level horizon.
    /// Heading stays on the compass. Returns whether a reference is stored.
    bool toggleHorizon();

    /// True when pitch and roll are drawn from a stored level reference.
    bool horizonSet() const { return mLevelSet; }

    /// GPS, gyro, accelerometer, or compass. Index 0 is GPS, then gyro, accelerometer, compass.
    bool sensorOn(int index) const;
    /// Flips one sensor into or out of the PX4 EKF. The SOURCES page still shows its reading.
    void toggleSensor(int index);

private:
    void publish();
    void fuse(const float sample[32]);
    /// Writes the display quaternion. After SET, pitch and roll follow gravity
    /// against the SET down. A turn about gravity does not bank the tape.
    void setDisplayQuat();

    AttitudeData mAttitude{};
    float mRawPitch = 0.0f;
    float mRawRoll = 0.0f;
    float mQuatW = 1.0f;
    float mQuatX = 0.0f;
    float mQuatY = 0.0f;
    float mQuatZ = 0.0f;
    bool mHaveQuat = false;
    float mPitchZero = 0.0f;
    float mRollZero = 0.0f;
    float mRefW = 1.0f;
    float mRefX = 0.0f;
    float mRefY = 0.0f;
    float mRefZ = 0.0f;
    bool mLevelSet = false;
    DynamicsData mDynamics{};
    EngineData mEngine{};
    LocationData mLocation{};
    bool mRunning = false;
    bool mNoted = false;
    bool mUseGps = true;
    bool mUseGyro = true;
    bool mUseAccel = true;
    bool mUseMag = true;
    uint64_t mTimeUs = 1000;
    uint64_t mLastGpsUs = 0;
    bool mMagFed = false;
    bool mGpsFed = false;
    bool mResetEkf = false;
    bool mLoggedReady = false;
    float mHoldAng[3]{};
    float mHoldVel[3]{};
    float mHoldDtAng = 0.0f;
    float mHoldDtVel = 0.0f;
    float mLastAccel = 0.0f;
    float mLastGyro = 0.0f;
    float mLastDtAng = 0.0f;
    float mLastDtVel = 0.0f;
    int mStatusTicks = 0;
    SensorReport mReport{};
};

#endif
