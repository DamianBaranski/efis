/// \file flight_plan.cpp
/// Route mutation and the leg / wind / bearing math shared by the planner and the HSI.
#include "flight_plan.h"

#include "geo_coord_utils.h"
#include "nav_db.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kNmPerMeter = 1.0 / 1852.0;

double deg2rad(double d) { return d * kPi / 180.0; }
double rad2deg(double r) { return r * 180.0 / kPi; }
} // namespace

FlightPlan::FlightPlan()
{
    clearRoute();
    // clearRoute() sets the default EPWR/EPKK pair; expand to the POC preset.
    mRoute = {"EPWR", "KUKUS", "DODUS", "EPKK"};
    mActiveLegIndex = 1;
    mAircraft = "C172";
    bump();
}

void FlightPlan::setRoute(std::vector<std::string> route)
{
    mRoute = std::move(route);
    if (mRoute.empty())
    {
        mActiveLegIndex = 0;
    }
    else
    {
        mActiveLegIndex = std::clamp(mActiveLegIndex, 0, static_cast<int>(mRoute.size()) - 1);
    }
    bump();
}

void FlightPlan::setWaypoint(int index, const std::string &ident)
{
    if (index < 0 || index >= static_cast<int>(mRoute.size()))
    {
        return;
    }
    mRoute[static_cast<size_t>(index)] = ident;
    bump();
}

void FlightPlan::insertBeforeDest(const std::string &ident)
{
    if (ident.empty())
    {
        return;
    }
    if (mRoute.size() < 2)
    {
        mRoute.push_back(ident);
    }
    else
    {
        // Same behaviour as handleWaypointSearch: pop dest, push new, push dest.
        std::string dest = mRoute.back();
        mRoute.pop_back();
        mRoute.push_back(ident);
        mRoute.push_back(dest);
    }
    bump();
}

void FlightPlan::moveWaypoint(int index, int dir)
{
    const int newIdx = index + dir;
    if (index < 0 || index >= static_cast<int>(mRoute.size()))
    {
        return;
    }
    if (newIdx < 0 || newIdx >= static_cast<int>(mRoute.size()))
    {
        return;
    }
    std::swap(mRoute[static_cast<size_t>(index)], mRoute[static_cast<size_t>(newIdx)]);
    bump();
}

void FlightPlan::deleteWaypoint(int index)
{
    if (index < 0 || index >= static_cast<int>(mRoute.size()))
    {
        return;
    }
    if (mRoute.size() <= 2)
    {
        return;
    }
    mRoute.erase(mRoute.begin() + index);
    if (mActiveLegIndex >= static_cast<int>(mRoute.size()))
    {
        mActiveLegIndex = static_cast<int>(mRoute.size()) - 1;
    }
    bump();
}

void FlightPlan::clearRoute()
{
    mRoute = {"EPWR", "EPKK"};
    mActiveLegIndex = 1;
    bump();
}

void FlightPlan::setActiveLegIndex(int index)
{
    if (mRoute.empty())
    {
        mActiveLegIndex = 0;
        return;
    }
    mActiveLegIndex = std::clamp(index, 0, static_cast<int>(mRoute.size()) - 1);
    bump();
}

void FlightPlan::setAircraft(const std::string &code)
{
    mAircraft = code;
    if (code == "C172")
    {
        mTasKt = 115.0f;
        mFuelBurnLph = 32.0f;
    }
    else if (code == "PA28")
    {
        mTasKt = 110.0f;
        mFuelBurnLph = 34.0f;
    }
    else if (code == "VL3")
    {
        mTasKt = 145.0f;
        mFuelBurnLph = 22.0f;
    }
    else if (code == "RV7")
    {
        mTasKt = 160.0f;
        mFuelBurnLph = 38.0f;
    }
    bump();
}

void FlightPlan::setTasKt(float v)
{
    mTasKt = std::clamp(v, 60.0f, 250.0f);
    bump();
}

void FlightPlan::setAltitudeFt(float v)
{
    mAltitudeFt = std::clamp(v, 1000.0f, 18000.0f);
    bump();
}

void FlightPlan::setFuelBurnLph(float v)
{
    mFuelBurnLph = std::clamp(v, 10.0f, 120.0f);
    bump();
}

void FlightPlan::setFuelOnBoardL(float v)
{
    mFuelOnBoardL = std::clamp(v, 20.0f, 250.0f);
    bump();
}

void FlightPlan::setPob(int v)
{
    mPob = std::clamp(v, 1, 6);
    bump();
}

void FlightPlan::setWindDirDeg(float v)
{
    float wrapped = std::fmod(v, 360.0f);
    if (wrapped < 0.0f)
    {
        wrapped += 360.0f;
    }
    mWindDirDeg = wrapped;
    bump();
}

void FlightPlan::setWindSpdKt(float v)
{
    mWindSpdKt = std::clamp(v, 0.0f, 60.0f);
    bump();
}

bool FlightPlan::armActiveLeg()
{
    if (mRoute.empty())
    {
        return false;
    }
    const int idx = std::clamp(mActiveLegIndex, 0, static_cast<int>(mRoute.size()) - 1);
    const int fromIdx = idx > 0 ? idx - 1 : idx;
    const std::string &toIdent = mRoute[static_cast<size_t>(idx)];
    const std::string &fromIdent = mRoute[static_cast<size_t>(fromIdx)];
    const Waypoint *to = NavDb::instance().find(toIdent);
    const Waypoint *from = NavDb::instance().find(fromIdent);
    if (to == nullptr || from == nullptr)
    {
        return false;
    }
    if (to->ident == from->ident)
    {
        return false;
    }
    mArmedFromIdent = from->ident;
    mArmedToIdent = to->ident;
    mArmedFromLat = from->lat;
    mArmedFromLon = from->lon;
    mArmedToLat = to->lat;
    mArmedToLon = to->lon;
    mArmed = true;
    bump();
    return true;
}

float FlightPlan::distanceNm(double lat1, double lon1, double lat2, double lon2)
{
    return static_cast<float>(std::round(GeoCoordUtils::calculateDistance(lat1, lon1, lat2, lon2) * kNmPerMeter));
}

float FlightPlan::bearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    const double lat1r = deg2rad(lat1);
    const double lat2r = deg2rad(lat2);
    const double dLon = deg2rad(lon2 - lon1);
    const double y = std::sin(dLon) * std::cos(lat2r);
    const double x = std::cos(lat1r) * std::sin(lat2r) - std::sin(lat1r) * std::cos(lat2r) * std::cos(dLon);
    double brg = rad2deg(std::atan2(y, x));
    brg = std::fmod(brg + 360.0, 360.0);
    return static_cast<float>(std::round(brg));
}

WindResult FlightPlan::windCorrection(float trackDeg, float tasKt, float windDirDeg, float windSpdKt)
{
    WindResult r;
    if (tasKt <= 0.0f)
    {
        r.headingDeg = trackDeg;
        r.groundSpeedKt = 20.0f;
        return r;
    }
    // In the POC the wind is a from-direction; the wind vector is directed
    // *away* from windDir (i.e. add 180 if you want the "to" bearing).
    // The convention below reproduces the POC math so ownship numbers line up.
    const double windAngle = deg2rad(static_cast<double>(windDirDeg - trackDeg));
    const double crosswind = static_cast<double>(windSpdKt) * std::sin(windAngle);
    const double headwind = static_cast<double>(windSpdKt) * std::cos(windAngle);
    const double ratio = crosswind / static_cast<double>(tasKt);
    double wcaRad = 0.0;
    if (std::fabs(ratio) <= 1.0)
    {
        wcaRad = std::asin(ratio);
    }
    const double wcaDeg = rad2deg(wcaRad);
    double heading = static_cast<double>(trackDeg) + wcaDeg;
    heading = std::fmod(heading + 360.0, 360.0);
    const double gs = static_cast<double>(tasKt) * std::cos(wcaRad) - headwind;
    r.headingDeg = static_cast<float>(std::round(heading));
    r.groundSpeedKt = static_cast<float>(std::max(20.0, std::round(gs)));
    r.crosswindKt = static_cast<float>(std::round(crosswind));
    r.headwindKt = static_cast<float>(std::round(headwind));
    r.wcaDeg = static_cast<float>(wcaDeg);
    return r;
}

void FlightPlan::bump()
{
    ++mRevision;
}
