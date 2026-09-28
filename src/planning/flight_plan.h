/// \file flight_plan.h
/// Route, performance, and armed leg used by the PLANNING mode and the HSI.

#ifndef FLIGHT_PLAN_H
#define FLIGHT_PLAN_H

#include <cstdint>
#include <string>
#include <vector>

/// Wind triangle result for one leg.
struct WindResult
{
    float headingDeg = 0.0f;     ///< Wind-corrected heading in degrees true.
    float groundSpeedKt = 0.0f;  ///< Ground speed in knots, floored at 20.
    float crosswindKt = 0.0f;    ///< Signed crosswind, positive from the right.
    float headwindKt = 0.0f;     ///< Signed headwind, positive on the nose.
    float wcaDeg = 0.0f;         ///< Wind correction angle in degrees.
};

/// One computed leg for the plan table.
struct LegRow
{
    std::string ident;      ///< Waypoint identifier.
    std::string name;       ///< Verbose name, may be empty.
    double lat = 0.0;       ///< Waypoint latitude in degrees.
    double lon = 0.0;       ///< Waypoint longitude in degrees.
    float elevationFt = 0.0f; ///< Field elevation in feet, or -1 for unknown.
    bool known = false;     ///< False when the ident was not in the database.
    float legDistNm = 0.0f; ///< Great-circle distance from the previous waypoint.
    float trackDeg = 0.0f;  ///< Initial great-circle course, degrees true.
    float headingDeg = 0.0f;///< Wind-corrected heading, degrees true.
    float groundSpeedKt = 0.0f; ///< Ground speed for this leg.
    float eteMin = 0.0f;    ///< Estimated time enroute in minutes.
    bool computed = false;  ///< False on the first row and when a fix is unknown.
};

/// Route, aircraft, and armed leg shared by the planner and the live instruments.
///
/// Mutation is manual; the planner rebuilds derived state after each edit. Values
/// are session-only. Defaults match the flight-plan creator POC.
class FlightPlan
{
public:
    /// Loads the default POC route and aircraft profile.
    FlightPlan();

    /// Route identifiers in order. First is departure, last is arrival.
    const std::vector<std::string> &route() const { return mRoute; }

    /// Overwrites the route. `activeLegIndex` is clamped after this call.
    void setRoute(std::vector<std::string> route);

    /// Replaces the ident at `index`.
    void setWaypoint(int index, const std::string &ident);

    /// Inserts `ident` before the destination when appropriate. Empty routes get
    /// the ident appended.
    void insertBeforeDest(const std::string &ident);

    /// Swaps the ident at `index` with the neighbour in `dir` (+1 or -1). Refuses
    /// out-of-range moves silently.
    void moveWaypoint(int index, int dir);

    /// Removes the ident at `index`. Refuses to leave fewer than two entries.
    void deleteWaypoint(int index);

    /// Resets the route to EPWR -> EPKK and puts the active leg on the arrival.
    void clearRoute();

    /// Index of the leg currently sequenced. 0 is the departure, so the leg being
    /// flown is (index-1) -> index.
    int activeLegIndex() const { return mActiveLegIndex; }

    /// Sets the sequenced leg, clamped to the route range.
    void setActiveLegIndex(int index);

    /// Aircraft profile identifier ("C172", "PA28", "VL3", "RV7").
    const std::string &aircraft() const { return mAircraft; }
    /// Applies a preset profile and updates TAS and burn.
    void setAircraft(const std::string &code);

    float tasKt() const { return mTasKt; }         ///< True airspeed in knots.
    void setTasKt(float v);
    float altitudeFt() const { return mAltitudeFt; } ///< Cruise altitude in feet.
    void setAltitudeFt(float v);
    float fuelBurnLph() const { return mFuelBurnLph; } ///< Burn in litres per hour.
    void setFuelBurnLph(float v);
    float fuelOnBoardL() const { return mFuelOnBoardL; } ///< Fuel on board in litres.
    void setFuelOnBoardL(float v);
    int pob() const { return mPob; }               ///< People on board.
    void setPob(int v);
    float windDirDeg() const { return mWindDirDeg; } ///< Wind from direction, true.
    void setWindDirDeg(float v);
    float windSpdKt() const { return mWindSpdKt; }  ///< Wind speed in knots.
    void setWindSpdKt(float v);

    /// True when Activate Route (or map Send to Avionics) has been pressed.
    bool armed() const { return mArmed; }
    /// Latitude of the armed TO waypoint.
    double armedToLat() const { return mArmedToLat; }
    /// Longitude of the armed TO waypoint.
    double armedToLon() const { return mArmedToLon; }
    /// Identifier of the armed TO waypoint.
    const std::string &armedToIdent() const { return mArmedToIdent; }
    /// Latitude of the armed FROM waypoint. Same as TO on the first leg.
    double armedFromLat() const { return mArmedFromLat; }
    /// Longitude of the armed FROM waypoint.
    double armedFromLon() const { return mArmedFromLon; }
    /// Identifier of the armed FROM waypoint.
    const std::string &armedFromIdent() const { return mArmedFromIdent; }

    /// Copies the current active leg into the armed slot. Returns true when a
    /// valid leg was armed (both fixes known and distinct).
    bool armActiveLeg();

    /// Global revision counter. Bumped after every mutation and every arming.
    /// Widgets can compare this to a cached value to know they need to redraw.
    std::uint64_t revision() const { return mRevision; }

    /// Great-circle distance in nautical miles, rounded to the nearest whole NM.
    static float distanceNm(double lat1, double lon1, double lat2, double lon2);

    /// Initial great-circle course in degrees true, rounded to the nearest degree.
    static float bearingDeg(double lat1, double lon1, double lat2, double lon2);

    /// Wind triangle for one leg. Returns TAS with no correction when the wind is
    /// weaker than TAS but with an unreachable crosswind. Ground speed is floored
    /// at 20 kt so ETE never divides by zero.
    static WindResult windCorrection(float trackDeg, float tasKt, float windDirDeg, float windSpdKt);

private:
    void bump();

    std::vector<std::string> mRoute;
    int mActiveLegIndex = 1;
    std::string mAircraft;
    float mTasKt = 115.0f;
    float mAltitudeFt = 4500.0f;
    float mFuelBurnLph = 32.0f;
    float mFuelOnBoardL = 100.0f;
    int mPob = 2;
    float mWindDirDeg = 260.0f;
    float mWindSpdKt = 14.0f;

    bool mArmed = false;
    std::string mArmedFromIdent;
    std::string mArmedToIdent;
    double mArmedFromLat = 0.0;
    double mArmedFromLon = 0.0;
    double mArmedToLat = 0.0;
    double mArmedToLon = 0.0;

    std::uint64_t mRevision = 0;
};

#endif
