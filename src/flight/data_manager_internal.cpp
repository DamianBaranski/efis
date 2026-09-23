/// \file data_manager_internal.cpp
/// Reads the tablet sensor sample and publishes attitude and GPS.
#include "data_manager_internal.h"
#include "sdl_compat.h"
#include <cmath>
#include <iostream>
#ifdef __ANDROID__
#include <jni.h>
#include "ekf.h"
#endif

namespace
{
#ifdef __ANDROID__
jclass gActivity = nullptr;
jmethodID gSetSensors = nullptr;
jmethodID gSample = nullptr;

bool bindSensors()
{
    if (gSetSensors && gSample)
    {
        return true;
    }
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env)
    {
        return false;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    jclass local = env->FindClass("com/efis/app/EfisActivity");
    if (!local)
    {
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        return false;
    }
    gActivity = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    gSetSensors = env->GetStaticMethodID(gActivity, "setTabletSensors", "(Z)V");
    if (!gSetSensors && env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    gSample = env->GetStaticMethodID(gActivity, "tabletSample", "()[F");
    if (!gSample && env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    return gActivity && gSetSensors && gSample;
}

void androidSet(bool on)
{
    if (!bindSensors())
    {
        std::cerr << "Internal sensors: tablet bridge missing" << std::endl;
        return;
    }
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env)
    {
        return;
    }
    env->CallStaticVoidMethod(gActivity, gSetSensors, on ? JNI_TRUE : JNI_FALSE);
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
}

/// 32 floats. 0-4 position and speed, 8 flags, 9-17 raw sensor display,
/// 18-20 gyro delta angle in the aircraft frame, 21-23 accelerometer delta velocity,
/// 24-25 the integration intervals in seconds, 26-28 compass in gauss,
/// 29 bearing degrees, 30 horizontal accuracy metres.
bool androidSample(float out[32])
{
    if (!bindSensors())
    {
        return false;
    }
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env)
    {
        return false;
    }
    jfloatArray array = static_cast<jfloatArray>(env->CallStaticObjectMethod(gActivity, gSample));
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return false;
    }
    if (!array)
    {
        return false;
    }
    const jsize count = env->GetArrayLength(array);
    const bool ok = count >= 32;
    if (ok)
    {
        env->GetFloatArrayRegion(array, 0, 32, out);
    }
    env->DeleteLocalRef(array);
    return ok;
}
#endif
}

const AttitudeData &DataManagerInternal::getAttitudeData() const
{
    return mAttitude;
}

const DynamicsData &DataManagerInternal::getDynamicsData() const
{
    return mDynamics;
}

const EngineData &DataManagerInternal::getEngineData() const
{
    return mEngine;
}

const LocationData &DataManagerInternal::getLocationData() const
{
    return mLocation;
}

void DataManagerInternal::seed(const AttitudeData &attitude, const DynamicsData &dynamics, const LocationData &location)
{
    mAttitude = attitude;
    mDynamics = dynamics;
    mDynamics.slip_rad = 0.0f;
    mLocation = location;
}

void DataManagerInternal::setRunning(bool on)
{
    if (mRunning == on)
    {
        return;
    }
    mRunning = on;
    if (!on)
    {
        mReport = {};
    }
    else
    {
        mResetEkf = true;
        mLoggedReady = false;
        mHoldDtAng = 0.0f;
        mHoldDtVel = 0.0f;
        mHoldAng[0] = mHoldAng[1] = mHoldAng[2] = 0.0f;
        mHoldVel[0] = mHoldVel[1] = mHoldVel[2] = 0.0f;
    }
#ifdef __ANDROID__
    androidSet(on);
    if (on && !mNoted)
    {
        mNoted = true;
        std::cout << "Internal sensors: GPS, gyro, accelerometer, compass" << std::endl;
    }
#else
    (void)on;
    if (mRunning && !mNoted)
    {
        mNoted = true;
        std::cout << "Internal sensors: GPS, gyro, accelerometer, and compass need the tablet" << std::endl;
    }
#endif
}

void DataManagerInternal::tick()
{
    if (!mRunning)
    {
        return;
    }
#ifdef __ANDROID__
    float sample[32] = {};
    if (!androidSample(sample))
    {
        return;
    }
    const int flags = static_cast<int>(sample[8]);
    mReport.gps = (flags & 1) != 0;
    mReport.lat = sample[0];
    mReport.lon = sample[1];
    mReport.alt = sample[2];
    mReport.speed = sample[3];
    mReport.gyroHw = (flags & 32) != 0;
    mReport.gyro = (flags & 4) != 0;
    mReport.gx = sample[9];
    mReport.gy = sample[10];
    mReport.gz = sample[11];
    mReport.accelHw = (flags & 64) != 0;
    mReport.accel = (flags & 8) != 0;
    mReport.ax = sample[12];
    mReport.ay = sample[13];
    mReport.az = sample[14];
    mReport.compassHw = (flags & 128) != 0;
    mReport.compass = (flags & 16) != 0;
    mReport.mx = sample[15];
    mReport.my = sample[16];
    mReport.mz = sample[17];
    mReport.attitude = false;
    fuse(sample);
    if (mReport.gps && mUseGps)
    {
        mLocation.latitude = sample[0];
        mLocation.longitude = sample[1];
        mLocation.altitude = sample[2];
        mDynamics.airspeed = sample[3];
        mDynamics.vertical_speed = sample[4];
        mDynamics.slip_rad = 0.0f;
    }
    publish();
#else
    publish();
#endif
}

bool DataManagerInternal::sensorOn(int index) const
{
    switch (index)
    {
    case 0:
        return mUseGps;
    case 1:
        return mUseGyro;
    case 2:
        return mUseAccel;
    case 3:
        return mUseMag;
    default:
        return false;
    }
}

void DataManagerInternal::toggleSensor(int index)
{
    switch (index)
    {
    case 0:
        mUseGps = !mUseGps;
        break;
    case 1:
        mUseGyro = !mUseGyro;
        break;
    case 2:
        mUseAccel = !mUseAccel;
        break;
    case 3:
        mUseMag = !mUseMag;
        break;
    default:
        break;
    }
}

void DataManagerInternal::fuse(const float sample[32])
{
#ifndef __ANDROID__
    (void)sample;
#else
    static Ekf *ekf = nullptr;
    static bool ready = false;
    if (mResetEkf)
    {
        delete ekf;
        ekf = nullptr;
        ready = false;
        mResetEkf = false;
        std::cout << "Internal EKF reset" << std::endl;
    }
    if (ekf == nullptr)
    {
        ekf = new Ekf();
    }
    if (!ready)
    {
        parameters *params = ekf->getParamHandle();
        params->ekf2_hgt_ref = static_cast<int32_t>(HeightSensor::GNSS);
        params->ekf2_mag_type = static_cast<int32_t>(MagFuseType::HEADING);
        params->ekf2_gps_ctrl = static_cast<int32_t>(GnssCtrl::HPOS) | static_cast<int32_t>(GnssCtrl::VPOS) |
                                static_cast<int32_t>(GnssCtrl::VEL);
        params->ekf2_gps_check = 0;
        params->ekf2_req_nsats = 0;
        params->ekf2_req_eph = 25.0f;
        params->ekf2_req_epv = 40.0f;
        // Under the 3 degree tilt-valid test, so LEVEL arms once gravity init succeeds.
        params->ekf2_angerr_init = 0.03f;
        ready = true;
    }
    parameters *params = ekf->getParamHandle();
    params->ekf2_mag_type = static_cast<int32_t>(mUseMag ? MagFuseType::HEADING : MagFuseType::NONE);
    params->ekf2_gps_ctrl = mUseGps ? (static_cast<int32_t>(GnssCtrl::HPOS) | static_cast<int32_t>(GnssCtrl::VPOS) |
                                       static_cast<int32_t>(GnssCtrl::VEL))
                                    : 0;
    const int32_t gravity = static_cast<int32_t>(ImuCtrl::GravityVector);
    if (mUseAccel)
    {
        params->ekf2_imu_ctrl |= gravity;
    }
    else
    {
        params->ekf2_imu_ctrl &= ~gravity;
    }
    const float dtAng = sample[24];
    const float dtVel = sample[25];
    // A switched-off side drops its chunk. The filter does not step without both.
    if (!mUseGyro)
    {
        mHoldAng[0] = mHoldAng[1] = mHoldAng[2] = 0.0f;
        mHoldDtAng = 0.0f;
    }
    if (!mUseAccel)
    {
        mHoldVel[0] = mHoldVel[1] = mHoldVel[2] = 0.0f;
        mHoldDtVel = 0.0f;
    }
    if (mUseGyro && dtAng > 0.0f && dtAng < 0.5f)
    {
        mHoldAng[0] += sample[18];
        mHoldAng[1] += sample[19];
        mHoldAng[2] += sample[20];
        mHoldDtAng += dtAng;
    }
    if (mUseAccel && dtVel > 0.0f && dtVel < 0.5f)
    {
        mHoldVel[0] += sample[21];
        mHoldVel[1] += sample[22];
        mHoldVel[2] += sample[23];
        mHoldDtVel += dtVel;
    }
    // Gyro and accelerometer are both required. A missing one stops the filter.
    const bool imuReady = mUseGyro && mUseAccel && mHoldDtAng > 0.004f && mHoldDtVel > 0.004f && mHoldDtAng < 0.5f &&
                          mHoldDtVel < 0.5f;
    if (imuReady)
    {
        const float accel = std::sqrt(mHoldVel[0] * mHoldVel[0] + mHoldVel[1] * mHoldVel[1] + mHoldVel[2] * mHoldVel[2]) /
                            mHoldDtVel;
        const float gyro = std::sqrt(mHoldAng[0] * mHoldAng[0] + mHoldAng[1] * mHoldAng[1] + mHoldAng[2] * mHoldAng[2]) /
                           mHoldDtAng;
        mLastAccel = accel;
        mLastGyro = gyro;
        mLastDtAng = mHoldDtAng;
        mLastDtVel = mHoldDtVel;
        const bool atRest = accel > 7.8f && accel < 11.8f && gyro < 0.25f;
        ekf->set_vehicle_at_rest(atRest);
        ekf->set_in_air_status(sample[3] > 15.0f);
        // The filter keeps 10 ms steps and clamps anything longer without scaling the
        // delta, which turns a real 1 g into about 2 g and blocks tilt alignment.
        constexpr float kSlice = 0.01f;
        for (int guard = 0; guard < 8 && mHoldDtAng >= 0.004f && mHoldDtVel >= 0.004f; ++guard)
        {
            const float use = std::min(kSlice, std::min(mHoldDtAng, mHoldDtVel));
            const float angScale = use / mHoldDtAng;
            const float velScale = use / mHoldDtVel;
            mTimeUs += static_cast<uint64_t>(use * 1000000.0f);
            imuSample imu{};
            imu.time_us = mTimeUs;
            imu.delta_ang = Vector3f(mHoldAng[0] * angScale, mHoldAng[1] * angScale, mHoldAng[2] * angScale);
            imu.delta_vel = Vector3f(mHoldVel[0] * velScale, mHoldVel[1] * velScale, mHoldVel[2] * velScale);
            imu.delta_ang_dt = use;
            imu.delta_vel_dt = use;
            for (int axis = 0; axis < 3; ++axis)
            {
                mHoldAng[axis] -= imu.delta_ang(axis);
                mHoldVel[axis] -= imu.delta_vel(axis);
            }
            mHoldDtAng -= use;
            mHoldDtVel -= use;
            ekf->setIMUData(imu);
            ekf->update();
        }
        if (mHoldDtAng < 0.004f && mHoldDtVel < 0.004f)
        {
            mHoldAng[0] = mHoldAng[1] = mHoldAng[2] = 0.0f;
            mHoldVel[0] = mHoldVel[1] = mHoldVel[2] = 0.0f;
            mHoldDtAng = 0.0f;
            mHoldDtVel = 0.0f;
        }
        if (mUseMag && mReport.compass)
        {
            magSample mag{};
            mag.time_us = mTimeUs;
            mag.mag = Vector3f(sample[26], sample[27], sample[28]);
            mag.reset = !mMagFed;
            mMagFed = true;
            ekf->setMagData(mag);
        }
        else
        {
            mMagFed = false;
        }
        if (mUseGps && mReport.gps && mTimeUs - mLastGpsUs > 200000)
        {
            gnssSample gps{};
            gps.time_us = mTimeUs;
            gps.lat = sample[0];
            gps.lon = sample[1];
            gps.alt = sample[2];
            gps.hacc = sample[30] > 0.3f ? sample[30] : 8.0f;
            gps.vacc = gps.hacc * 1.5f;
            gps.sacc = 0.5f;
            gps.fix_type = 3;
            gps.nsats = 10;
            gps.pdop = 1.5f;
            gps.yaw = NAN;
            if (std::isfinite(sample[29]))
            {
                const float bearing = sample[29] * 0.0174532925f;
                gps.vel(0) = sample[3] * std::cos(bearing);
                gps.vel(1) = sample[3] * std::sin(bearing);
            }
            gps.vel(2) = -sample[4];
            ekf->setGpsData(gps);
            mGpsFed = true;
            mLastGpsUs = mTimeUs;
        }
        else if (!mUseGps)
        {
            mGpsFed = false;
        }
    }
    else if (mHoldDtAng > 0.5f || mHoldDtVel > 0.5f)
    {
        std::cout << "Internal IMU dropped dtA=" << mHoldDtAng << " dtV=" << mHoldDtVel << std::endl;
        mHoldAng[0] = mHoldAng[1] = mHoldAng[2] = 0.0f;
        mHoldVel[0] = mHoldVel[1] = mHoldVel[2] = 0.0f;
        mHoldDtAng = 0.0f;
        mHoldDtVel = 0.0f;
    }
    if (mUseGyro && mUseAccel && ekf->attitude_valid())
    {
        const Eulerf euler(ekf->getQuaternion());
        mRawPitch = euler.theta();
        mRawRoll = euler.phi();
        float heading = euler.psi();
        if (heading < 0.0f)
        {
            heading += 6.283185307f;
        }
        mAttitude.heading = heading;
        mAttitude.pitch = mRawPitch - mPitchZero;
        mAttitude.roll = mRawRoll - mRollZero;
        const Quatf raw = ekf->getQuaternion();
        mQuatW = raw(0);
        mQuatX = raw(1);
        mQuatY = raw(2);
        mQuatZ = raw(3);
        mHaveQuat = true;
        setDisplayQuat();
        mReport.attitude = true;
        mReport.headingDeg = heading * 57.2957795f;
        if (!mLoggedReady)
        {
            mLoggedReady = true;
            std::cout << "Internal attitude ready pitch=" << mRawPitch << " roll=" << mRawRoll << std::endl;
        }
    }
    if (++mStatusTicks >= 40)
    {
        mStatusTicks = 0;
        std::cout << "Internal gps=" << mReport.gps << " gyro=" << mReport.gyro << " accel=" << mReport.accel
                  << " mag=" << mReport.compass << " useG=" << mUseGyro << " useA=" << mUseAccel
                  << " useM=" << mUseMag << " useP=" << mUseGps << " fedA=" << mLastDtAng << " fedV=" << mLastDtVel
                  << " |a|=" << mLastAccel << " |w|=" << mLastGyro << " tilt=" << mReport.attitude
                  << " holdA=" << mHoldDtAng << " holdV=" << mHoldDtVel << std::endl;
    }
#endif
}

bool DataManagerInternal::toggleHorizon()
{
    if (!mReport.attitude || !mHaveQuat)
    {
        std::cout << "LEVEL waiting for attitude" << std::endl;
        return false;
    }
    mPitchZero = mRawPitch;
    mRollZero = mRawRoll;
    mRefW = mQuatW;
    mRefX = mQuatX;
    mRefY = mQuatY;
    mRefZ = mQuatZ;
    mLevelSet = true;
    mAttitude.pitch = 0.0f;
    mAttitude.roll = 0.0f;
    setDisplayQuat();
    publish();
    std::cout << "Horizon set to current attitude" << std::endl;
    return true;
}

void DataManagerInternal::setDisplayQuat()
{
#ifdef __ANDROID__
    if (!mHaveQuat)
    {
        mAttitude.useQuat = false;
        return;
    }
    Quatf q;
    q(0) = mQuatW;
    q(1) = mQuatX;
    q(2) = mQuatY;
    q(3) = mQuatZ;
    if (mLevelSet)
    {
        Quatf ref;
        ref(0) = mRefW;
        ref(1) = mRefX;
        ref(2) = mRefY;
        ref(3) = mRefZ;
        const Vector3f nose = ref.rotateVector(Vector3f(1.0f, 0.0f, 0.0f));
        const float heading = std::atan2(nose(1), nose(0));
        q = Quatf(Eulerf(0.0f, 0.0f, heading)) * ref.inversed() * q;
    }
    mAttitude.qw = q(0);
    mAttitude.qx = q(1);
    mAttitude.qy = q(2);
    mAttitude.qz = q(3);
    mAttitude.useQuat = true;
#else
    mAttitude.useQuat = false;
#endif
}

void DataManagerInternal::publish()
{
    notify(DataType::ATTITUDE_DATA);
    notify(DataType::LOCATION_DATA);
    notify(DataType::DYNAMICS_DATA);
}
