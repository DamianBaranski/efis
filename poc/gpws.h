/// \file gpws.h
/// Ground-proximity cautions and warnings from GPS, runways, and obstacles.
#ifndef GPWS_H
#define GPWS_H

#include "bucket_container.h"
#include "data_type.h"
#include "obstacle_overlay.h"
#include "runway_overlay.h"
#include <vector>

/// Class B style alerts that this EFIS can compute without a radio altimeter.
/// Gear, flaps, glideslope, and windshear are not included.
/// Look-ahead terrain and terrain closure sample the loaded terrain mesh.
/// Phrases use the GPWS voice, which is independent of the navigation voice.
class Gpws
{
public:
    /// Process-wide alert logic.
    static Gpws &instance();

    /// Master switch. Off still tracks state and speaks nothing.
    void setEnabled(bool on) { mEnabled = on; }
    /// Obstacle caution and warning. The distance advisory stays on the sound page.
    void setObstacles(bool on) { mObstacles = on; }
    /// Look-ahead terrain along the track.
    void setTerrain(bool on) { mTerrain = on; }
    /// Excessive terrain closure from height above the mesh.
    void setClosure(bool on) { mClosure = on; }
    /// Excessive descent rate against height above the nearest runway.
    void setSinkRate(bool on) { mSinkRate = on; }
    /// Premature descent and the airport clearance floor.
    void setTooLow(bool on) { mTooLow = on; }
    /// Altitude loss after a geometric takeoff.
    void setDontSink(bool on) { mDontSink = on; }
    /// Callout crossing 500 feet above the runway on approach.
    void setFiveHundred(bool on) { mFiveHundred = on; }
    /// Excessive bank.
    void setBank(bool on) { mBank = on; }

    bool enabled() const { return mEnabled; }
    bool obstacles() const { return mObstacles; }
    bool terrain() const { return mTerrain; }
    bool closure() const { return mClosure; }
    bool sinkRate() const { return mSinkRate; }
    bool tooLow() const { return mTooLow; }
    bool dontSink() const { return mDontSink; }
    bool fiveHundred() const { return mFiveHundred; }
    bool bank() const { return mBank; }

    /// True while an obstacle caution or warning is the active ground-proximity condition.
    bool obstacleAlert() const { return mObstacleHot; }

    /// Speaks a short test phrase on the GPWS voice.
    void preview();

    /// Steps the alert logic from the latest situation and the catalogs already in memory.
    void update(const LocationData &location, const DynamicsData &dynamics, const AttitudeData &attitude,
                const std::vector<ObstacleOverlay::Cue> &obstacles, const std::vector<RunwayOverlay::Strip> &runways,
                const BucketContainer &terrain);

private:
    Gpws() = default;

    bool mEnabled = true;
    bool mObstacles = true;
    bool mTerrain = true;
    bool mClosure = true;
    bool mSinkRate = true;
    bool mTooLow = true;
    bool mDontSink = true;
    bool mFiveHundred = true;
    bool mBank = true;
    bool mObstacleHot = false;
};

#endif
