/// \file session.h
/// One run's situation feed: keyboard simulator, tablet sensors, or Stratux.

#ifndef SESSION_H
#define SESSION_H

#include "idata_manager.h"
#include <memory>

class Frame;

/// Which situation feed the instruments read.
enum class SituationSource
{
    Sim,      ///< Keyboard aircraft.
    Internal, ///< Tablet GPS, gyro, accelerometer, and compass.
    Stratux,  ///< Live Stratux receiver. Desktop only.
};

/// Latest tablet sample for the SOURCES page. Angles that are rates are radians per second.
struct SensorReport
{
    bool gps = false;     ///< A position fix is in lat, lon, alt, and speed.
    float lat = 0.0f;     ///< Degrees, north positive.
    float lon = 0.0f;     ///< Degrees, east positive.
    float alt = 0.0f;     ///< Metres above the ellipsoid.
    float speed = 0.0f;   ///< Ground speed, metres per second.
    int satellitesUsed = -1; ///< Satellites used in the fix. Negative when unknown.
    int satellitesSeen = -1; ///< Satellites in view. Negative when unknown.
    bool gyroHw = false;  ///< The tablet has a gyroscope.
    bool gyro = false;    ///< gx, gy, gz hold a reading.
    float gx = 0.0f;      ///< Radians per second, device X.
    float gy = 0.0f;      ///< Radians per second, device Y.
    float gz = 0.0f;      ///< Radians per second, device Z.
    bool accelHw = false; ///< The tablet has an accelerometer.
    bool accel = false;   ///< ax, ay, az hold a reading.
    float ax = 0.0f;      ///< Metres per second squared, device X.
    float ay = 0.0f;      ///< Metres per second squared, device Y.
    float az = 0.0f;      ///< Metres per second squared, device Z.
    bool compassHw = false; ///< The tablet has a magnetometer.
    bool compass = false;   ///< mx, my, mz hold a reading.
    float mx = 0.0f;        ///< Microtesla, device X.
    float my = 0.0f;        ///< Microtesla, device Y.
    float mz = 0.0f;        ///< Microtesla, device Z.
    bool attitude = false;  ///< headingDeg comes from the fused attitude.
    float headingDeg = 0.0f; ///< Degrees clockwise from magnetic north.
};

/// Starts and steps the situation feed for one run.
class ISession
{
public:
    virtual ~ISession() = default;

    /// Attitude, dynamics, engine, and position. Outlives the widgets that read it.
    virtual IDataManager &data() = 0;

    /// Starts the feed. The simulator parks at home. Stratux opens the HTTP poll.
    virtual void start() = 0;

    /// Steps the active feed for this frame. Stratux does nothing here.
    virtual void tick() = 0;

    /// Feed the instruments are reading.
    virtual SituationSource source() const = 0;

    /// Switches the feed. SIM is the keyboard. INTERNAL is the tablet sensors.
    virtual void setSource(SituationSource source) = 0;

    /// True when this run can select the Stratux receiver.
    virtual bool receiver() const = 0;

    /// Latest GPS, gyro, accelerometer, and compass sample. Empty on the desktop.
    virtual SensorReport sensors() const = 0;

    /// Stores the attitude being held as the level horizon.
    /// Heading is left on the compass. Returns whether a reference is stored.
    virtual bool toggleHorizon() = 0;

    /// True after LEVEL has stored a pitch and roll reference.
    virtual bool horizonSet() const = 0;

    /// True when this sensor is fed to the attitude filter. 0 GPS, 1 gyro, 2 accelerometer, 3 compass.
    virtual bool sensorOn(int index) const = 0;

    /// Flips one sensor into or out of the attitude filter.
    virtual void toggleSensor(int index) = 0;
};

/// Opens the simulator, or live Stratux when liveStratux is true.
/// Android always opens the simulator. The SOURCES tab can switch to the tablet sensors.
/// \param frame Loop the simulator keys register with.
std::unique_ptr<ISession> openSession(Frame &frame, bool liveStratux);

#endif
