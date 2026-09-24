/// \file gpws.cpp
/// Caution and warning envelopes for the alerts that GPS, runways, and obstacles can support.
#include "gpws.h"
#include "alarm_tone.h"
#include "geo_coord_utils.h"
#include "voice_announcer.h"

#include "sdl_compat.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kFt = 0.3048f;
constexpr float kNm = 1852.0f;
constexpr int kGpwsVoice = 1;
constexpr uint32_t kWarnRepeatMs = 8000;
constexpr uint32_t kCautionRepeatMs = 12000;

float geoidMetres(double latitude, double longitude)
{
    const float dLat = static_cast<float>(latitude - 50.0);
    const float dLon = static_cast<float>(longitude - 16.0);
    return 44.0f - 1.1f * dLat - 0.45f * dLon;
}

float wrap360(float degrees)
{
    degrees = std::fmod(degrees, 360.0f);
    if (degrees < 0.0f)
    {
        degrees += 360.0f;
    }
    return degrees;
}

float angleDelta(float a, float b)
{
    float delta = std::fabs(wrap360(a) - wrap360(b));
    if (delta > 180.0f)
    {
        delta = 360.0f - delta;
    }
    return delta;
}

float headingDegrees(float radians)
{
    return wrap360(radians * 180.0f / kPi);
}

double bearingDegrees(double lat1, double lon1, double lat2, double lon2)
{
    const double phi1 = lat1 * kPi / 180.0;
    const double phi2 = lat2 * kPi / 180.0;
    const double dLon = (lon2 - lon1) * kPi / 180.0;
    const double y = std::sin(dLon) * std::cos(phi2);
    const double x = std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dLon);
    return wrap360(static_cast<float>(std::atan2(y, x) * 180.0 / kPi));
}

struct Field
{
    bool valid = false;
    double distM = 1.0e12;
    float elevM = 0.0f;
    float headingDeg = 0.0f;
    float alignDeg = 180.0f;
    bool aligned = false;
};

Field nearestField(double latitude, double longitude, float headingDeg, const std::vector<RunwayOverlay::Strip> &runways,
                   double limitM)
{
    Field best;
    for (const auto &strip : runways)
    {
        const double dist = GeoCoordUtils::calculateDistance(latitude, longitude, strip.latitude, strip.longitude);
        if (dist > limitM || dist >= best.distM)
        {
            continue;
        }
        const float recip = wrap360(strip.headingDeg + 180.0f);
        const float toField = static_cast<float>(bearingDegrees(latitude, longitude, strip.latitude, strip.longitude));
        const float alignRunway = angleDelta(headingDeg, strip.headingDeg);
        const float alignRecip = angleDelta(headingDeg, recip);
        const bool towardRunway = alignRunway <= alignRecip;
        const float course = towardRunway ? strip.headingDeg : recip;
        best.valid = true;
        best.distM = dist;
        best.elevM = strip.elevationM;
        best.headingDeg = course;
        best.alignDeg = std::min(alignRunway, alignRecip);
        best.aligned = best.alignDeg <= 40.0f && angleDelta(toField, course) <= 30.0f;
    }
    return best;
}

enum class Level
{
    Clear,
    Caution,
    Warning,
};

struct Gate
{
    Level level = Level::Clear;
    float pen = 0.0f;
    uint32_t spokeMs = 0;
};

struct Phrase
{
    int rank = 100;
    int priority = 0;
    const char *text = "";
    bool speak = false;
};

struct Proposal
{
    Phrase phrase;
    Gate gate;
};

bool due(const Gate &gate, uint32_t now, uint32_t gap)
{
    return gate.spokeMs == 0 || now - gate.spokeMs >= gap;
}

Proposal consider(const Gate &gate, Level now, float pen, uint32_t timeMs, int rank, const char *caution,
                  const char *warning, bool repeatCaution)
{
    Proposal out;
    out.gate = gate;
    out.phrase.rank = rank;
    if (now == Level::Clear)
    {
        out.gate = {};
        return out;
    }
    const bool entered = now != gate.level && (gate.level == Level::Clear || now == Level::Warning);
    const bool deeper = now == gate.level && pen > gate.pen + 0.2f;
    const bool warnAgain = now == Level::Warning && due(gate, timeMs, kWarnRepeatMs);
    const bool cautionAgain = now == Level::Caution && repeatCaution && due(gate, timeMs, kCautionRepeatMs);
    if (entered || deeper || warnAgain || cautionAgain)
    {
        out.phrase.speak = true;
        out.phrase.priority = now == Level::Warning ? 1 : 0;
        out.phrase.text = now == Level::Warning ? warning : caution;
        out.gate.spokeMs = timeMs;
        out.gate.pen = pen;
    }
    else if (now == gate.level && pen < gate.pen)
    {
        out.gate.pen = pen;
    }
    out.gate.level = now;
    return out;
}

void choose(Phrase &best, const Phrase &next)
{
    if (!next.speak)
    {
        return;
    }
    if (!best.speak || next.rank < best.rank)
    {
        best = next;
    }
}

void keep(Gate &slot, const Proposal &proposal, const Phrase &best)
{
    const bool won = proposal.phrase.speak && best.speak && proposal.phrase.rank == best.rank &&
                     proposal.phrase.text == best.text;
    if (!proposal.phrase.speak || won)
    {
        slot = proposal.gate;
        return;
    }
    slot.level = proposal.gate.level;
    slot.pen = proposal.gate.pen;
}

struct TonePick
{
    AlarmChord chord = AlarmChord::None;
    int level = 0;
    int rank = 100;
};

void preferTone(TonePick &pick, AlarmChord chord, Level level, int warnRank, int cautionRank)
{
    if (level == Level::Clear)
    {
        return;
    }
    const int rank = level == Level::Warning ? warnRank : cautionRank;
    if (rank < pick.rank)
    {
        pick.chord = chord;
        pick.level = level == Level::Warning ? 2 : 1;
        pick.rank = rank;
    }
}
} // namespace

Gpws &Gpws::instance()
{
    static Gpws gpws;
    return gpws;
}

void Gpws::preview()
{
    VoiceAnnouncer::instance().say("Ground proximity warning ready.", 1, kGpwsVoice);
    AlarmTone::instance().demo();
}

void Gpws::update(const LocationData &location, const DynamicsData &dynamics, const AttitudeData &attitude,
                  const std::vector<ObstacleOverlay::Cue> &obstacles, const std::vector<RunwayOverlay::Strip> &runways,
                  const BucketContainer &terrain)
{
    static Gate sSink;
    static Gate sTooLow;
    static Gate sDont;
    static Gate sBank;
    static Gate sObstacle;
    static Gate sClosure;
    static Gate sAhead;
    static bool sFiveArmed = true;
    static int sTakeoff = 0;
    static float sPeakFt = 0.0f;
    static float sPrevAgl = 0.0f;
    static uint32_t sPrevAglMs = 0;
    static bool sHaveAgl = false;

    mObstacleHot = false;
    if (!mEnabled)
    {
        sSink = {};
        sTooLow = {};
        sDont = {};
        sBank = {};
        sObstacle = {};
        sClosure = {};
        sAhead = {};
        sFiveArmed = true;
        sTakeoff = 0;
        sHaveAgl = false;
        AlarmTone::instance().set(AlarmChord::None, 0);
        return;
    }

    const uint32_t now = SDL_GetTicks();
    const float heading = headingDegrees(attitude.heading);
    const float msl = location.altitude - geoidMetres(location.latitude, location.longitude);
    const float gs = std::max(0.0f, dynamics.airspeed);
    const float vs = dynamics.vertical_speed;
    const bool moving = gs >= 10.0f;
    const Field field = nearestField(location.latitude, location.longitude, heading, runways, 15.0 * kNm);
    const float aalM = field.valid ? msl - field.elevM : 0.0f;
    const float aalFt = aalM / kFt;
    const float distNm = field.valid ? static_cast<float>(field.distM / kNm) : 99.0f;

    Phrase best;
    Proposal obstacleProposal;
    Proposal sinkProposal;
    Proposal tooLowProposal;
    Proposal dontProposal;
    Proposal bankProposal;
    Proposal closureProposal;
    Proposal aheadProposal;
    bool haveObstacle = false;
    bool haveSink = false;
    bool haveTooLow = false;
    bool haveDont = false;
    bool haveBank = false;
    bool haveClosure = false;
    bool haveAhead = false;
    bool fiveCall = false;

    if (mObstacles && moving)
    {
        float worstClear = 1.0e6f;
        float worstT = 99.0f;
        bool threat = false;
        for (const auto &point : obstacles)
        {
            if (point.heightM <= 1.0f || point.elevationM <= 0.0f)
            {
                continue;
            }
            const double dist = GeoCoordUtils::calculateDistance(location.latitude, location.longitude, point.latitude,
                                                                 point.longitude);
            if (dist > 6.0 * kNm)
            {
                continue;
            }
            const float to = static_cast<float>(bearingDegrees(location.latitude, location.longitude, point.latitude,
                                                                point.longitude));
            const float rel = (to - heading) * kPi / 180.0f;
            const float along = static_cast<float>(dist) * std::cos(rel);
            const float across = std::fabs(static_cast<float>(dist) * std::sin(rel));
            if (along < -80.0f)
            {
                continue;
            }
            const float seconds = gs > 1.0f ? along / gs : 99.0f;
            const float predicted = msl + vs * std::min(seconds, 90.0f);
            const float clearance = predicted - (point.elevationM + point.heightM);
            const bool soon = seconds <= 60.0f && across < 350.0f && clearance < 91.0f;
            const bool beside = along < 700.0f && across < 350.0f && (msl - point.elevationM - point.heightM) < 91.0f;
            if (!soon && !beside)
            {
                continue;
            }
            threat = true;
            if (clearance < worstClear)
            {
                worstClear = clearance;
                worstT = seconds;
            }
        }
        Level level = Level::Clear;
        float pen = 0.0f;
        if (threat)
        {
            const bool warning = worstT <= 30.0f && worstClear < 30.0f;
            level = warning ? Level::Warning : Level::Caution;
            pen = warning ? std::max(0.0f, (30.0f - worstClear) / 30.0f) : std::max(0.0f, (91.0f - worstClear) / 91.0f);
            mObstacleHot = true;
        }
        const int rank = level == Level::Warning ? 2 : 3;
        obstacleProposal = consider(sObstacle, level, pen, now, rank, "Caution, obstacle.", "Obstacle, obstacle, pull up.",
                                    true);
        choose(best, obstacleProposal.phrase);
        haveObstacle = true;
    }
    else
    {
        sObstacle = {};
    }

    float here = 0.0f;
    const bool haveGround = terrain.groundMetres(location.latitude, location.longitude, here);
    const bool visualSegment = field.valid && field.aligned && distNm < 1.0f;
    if (mClosure && moving && haveGround)
    {
        const float agl = location.altitude - here;
        if (!sHaveAgl)
        {
            sPrevAgl = agl;
            sPrevAglMs = now;
            sHaveAgl = true;
        }
        else
        {
            const float dt = static_cast<float>(now - sPrevAglMs) / 1000.0f;
            if (dt >= 0.2f)
            {
                const float aglFtG = agl / kFt;
                const bool stableApproach = visualSegment && vs > -6.0f;
                if (dt <= 1.5f && !stableApproach && aglFtG > 30.0f && aglFtG < 2500.0f)
                {
                    const float closureFpm = (sPrevAgl - agl) / kFt * 60.0f / dt;
                    const float outer = 800.0f + aglFtG;
                    const float inner = 1700.0f + aglFtG;
                    Level level = Level::Clear;
                    float pen = 0.0f;
                    if (closureFpm >= inner)
                    {
                        level = Level::Warning;
                        pen = (closureFpm - inner) / inner;
                    }
                    else if (closureFpm >= outer)
                    {
                        level = Level::Caution;
                        pen = (closureFpm - outer) / outer;
                    }
                    closureProposal =
                        consider(sClosure, level, pen, now, level == Level::Warning ? 2 : 3, "Terrain, terrain.",
                                 "Pull up.", false);
                    choose(best, closureProposal.phrase);
                    haveClosure = true;
                }
                else
                {
                    sClosure = {};
                }
                sPrevAgl = agl;
                sPrevAglMs = now;
            }
        }
    }
    else
    {
        sHaveAgl = false;
        sClosure = {};
    }

    if (mTerrain && moving && !visualSegment)
    {
        const bool descending = vs < -0.5f;
        const bool departure = field.valid && vs > 1.0f && aalFt < 1000.0f && distNm < 6.0f;
        const double north = std::cos(attitude.heading);
        const double east = std::sin(attitude.heading);
        float worstMargin = 1.0e6f;
        float worstT = 99.0f;
        bool threat = false;
        bool soon = false;
        if (haveGround && location.altitude - here < 30.0f)
        {
            threat = true;
            soon = true;
            worstT = 0.0f;
            worstMargin = (location.altitude - here) - 30.0f;
        }
        for (int step = 1; step <= 6; ++step)
        {
            const float seconds = 10.0f * static_cast<float>(step);
            const double along = static_cast<double>(gs) * seconds;
            const double cross = std::min(400.0, 0.08 * along);
            const int spreads = seconds >= 20.0f ? 3 : 1;
            const double side[3] = {0.0, cross, -cross};
            for (int spread = 0; spread < spreads; ++spread)
            {
                const auto point = GeoCoordUtils::offsetMeters(location.latitude, location.longitude,
                                                               north * along - east * side[spread],
                                                               east * along + north * side[spread]);
                float ground = 0.0f;
                if (!terrain.groundMetres(point.latitude, point.longitude, ground))
                {
                    continue;
                }
                const float predicted = location.altitude + vs * seconds;
                const float clearance = predicted - ground;
                float rtc = (descending ? 500.0f : 700.0f) * kFt;
                if (departure)
                {
                    rtc = 100.0f * kFt;
                    if (clearance > 400.0f * kFt)
                    {
                        continue;
                    }
                }
                else if (field.valid && field.aligned && distNm < 5.0f)
                {
                    rtc = (descending ? 100.0f : 150.0f) * kFt;
                }
                else if (field.valid && distNm < 15.0f)
                {
                    rtc = (descending ? 300.0f : 350.0f) * kFt;
                }
                if (clearance >= rtc)
                {
                    continue;
                }
                threat = true;
                const float margin = clearance - rtc;
                if (margin < worstMargin)
                {
                    worstMargin = margin;
                    worstT = seconds;
                }
                if (seconds <= 30.0f)
                {
                    soon = true;
                }
            }
        }
        Level level = Level::Clear;
        float pen = 0.0f;
        if (threat)
        {
            level = soon || worstT <= 30.0f ? Level::Warning : Level::Caution;
            const float rtc = std::max(30.0f, 150.0f * kFt);
            pen = std::max(0.0f, -worstMargin / rtc);
        }
        aheadProposal = consider(sAhead, level, pen, now, level == Level::Warning ? 2 : 3, "Caution, terrain.",
                                 "Terrain, terrain, pull up.", true);
        choose(best, aheadProposal.phrase);
        haveAhead = true;
    }
    else
    {
        sAhead = {};
    }

    if (field.valid && moving)
    {
        if (mSinkRate && aalFt > 50.0f && aalFt < 5000.0f)
        {
            const float descent = std::max(0.0f, -vs / kFt * 60.0f);
            const float outer = 1000.0f + aalFt;
            const float inner = 2000.0f + aalFt;
            Level level = Level::Clear;
            float pen = 0.0f;
            if (descent >= inner)
            {
                level = Level::Warning;
                pen = (descent - inner) / inner;
            }
            else if (descent >= outer)
            {
                level = Level::Caution;
                pen = (descent - outer) / outer;
            }
            sinkProposal = consider(sSink, level, pen, now, level == Level::Warning ? 1 : 6, "Sink rate.", "Pull up.",
                                    false);
            choose(best, sinkProposal.phrase);
            haveSink = true;
        }
        else
        {
            sSink = {};
        }

        const float pathFt = static_cast<float>(field.distM) * std::tan(3.0f * kPi / 180.0f) / kFt;
        float required = 0.0f;
        if (mTooLow && distNm >= 0.4f && distNm <= 12.0f && aalFt > 40.0f)
        {
            float floorFt = 0.0f;
            if (distNm < 5.0f)
            {
                floorFt = 100.0f * distNm;
            }
            else
            {
                floorFt = 500.0f + 70.0f * (distNm - 5.0f);
            }
            if (field.aligned && distNm >= 1.0f && distNm <= 10.0f)
            {
                const float pda = static_cast<float>(field.distM) * std::tan(1.2f * kPi / 180.0f) / kFt;
                if (aalFt < pathFt - 200.0f)
                {
                    floorFt = std::max(floorFt, pda);
                }
            }
            required = floorFt;
        }
        if (mTooLow && required > 1.0f && aalFt < required)
        {
            const float pen = (required - aalFt) / required;
            tooLowProposal = consider(sTooLow, Level::Caution, pen, now, 4, "Too low, terrain.", "Too low, terrain.", false);
            choose(best, tooLowProposal.phrase);
            haveTooLow = true;
        }
        else
        {
            sTooLow = {};
        }

        if (mDontSink && distNm < 4.0f)
        {
            if (sTakeoff == 0 && aalFt < 120.0f)
            {
                sTakeoff = 1;
            }
            else if (sTakeoff == 1 && aalFt > 160.0f && vs > 1.0f)
            {
                sTakeoff = 2;
                sPeakFt = aalFt;
            }
            else if (sTakeoff == 2)
            {
                sPeakFt = std::max(sPeakFt, aalFt);
                if (aalFt > 900.0f || distNm > 5.0f)
                {
                    sTakeoff = 0;
                    sDont = {};
                }
                else
                {
                    const float lost = sPeakFt - aalFt;
                    const float limit = std::max(60.0f, 0.2f * sPeakFt);
                    if (lost > limit && vs < -0.5f)
                    {
                        const float pen = (lost - limit) / limit;
                        dontProposal = consider(sDont, Level::Caution, pen, now, 7, "Don't sink.", "Don't sink.", false);
                        choose(best, dontProposal.phrase);
                        haveDont = true;
                    }
                    else
                    {
                        sDont = {};
                    }
                }
            }
        }
        else if (distNm >= 4.0f)
        {
            sTakeoff = 0;
            sDont = {};
        }

        if (mFiveHundred && field.aligned && distNm < 8.0f && vs < -0.4f)
        {
            if (sFiveArmed && aalFt <= 500.0f && aalFt >= 350.0f)
            {
                Phrase call;
                call.speak = true;
                call.rank = 5;
                call.priority = 0;
                call.text = "Five hundred.";
                choose(best, call);
                sFiveArmed = false;
                fiveCall = true;
            }
            if (aalFt > 650.0f)
            {
                sFiveArmed = true;
            }
        }
        else if (aalFt > 650.0f || !field.aligned)
        {
            sFiveArmed = true;
        }
    }
    else
    {
        sSink = {};
        sTooLow = {};
        sDont = {};
        sTakeoff = 0;
        sFiveArmed = true;
    }

    if (mBank && moving)
    {
        float limit = 50.0f;
        if (field.valid && aalFt >= 10.0f && aalFt < 210.0f)
        {
            const float t = (aalFt - 10.0f) / 200.0f;
            limit = 15.0f + t * 35.0f;
        }
        const float roll = std::fabs(attitude.roll) * 180.0f / kPi;
        if (!field.valid || aalFt >= 10.0f)
        {
            if (roll >= limit)
            {
                const float pen = (roll - limit) / limit;
                const bool continuous = pen >= 0.44f;
                bankProposal = consider(sBank, continuous ? Level::Warning : Level::Caution, pen, now, 8,
                                        "Bank angle, bank angle.", "Bank angle, bank angle.", false);
                bankProposal.phrase.priority = 0;
                choose(best, bankProposal.phrase);
                haveBank = true;
            }
            else
            {
                sBank = {};
            }
        }
        else
        {
            sBank = {};
        }
    }
    else
    {
        sBank = {};
    }

    if (haveObstacle)
    {
        keep(sObstacle, obstacleProposal, best);
    }
    if (haveSink)
    {
        keep(sSink, sinkProposal, best);
    }
    if (haveTooLow)
    {
        keep(sTooLow, tooLowProposal, best);
    }
    if (haveDont)
    {
        keep(sDont, dontProposal, best);
    }
    if (haveBank)
    {
        keep(sBank, bankProposal, best);
    }
    if (haveClosure)
    {
        keep(sClosure, closureProposal, best);
    }
    if (haveAhead)
    {
        keep(sAhead, aheadProposal, best);
    }

    if (best.speak && best.text != nullptr && best.text[0] != '\0')
    {
        static uint32_t sSpokenMs = 0;
        static int sSpokenPriority = -1;
        const bool warning = best.priority >= 1;
        const bool escalate = warning && sSpokenPriority < 1;
        const uint32_t gap = warning ? kWarnRepeatMs : kCautionRepeatMs;
        if (escalate || sSpokenMs == 0 || now - sSpokenMs >= gap)
        {
            sSpokenMs = now;
            sSpokenPriority = best.priority;
            VoiceAnnouncer::instance().say(best.text, best.priority, kGpwsVoice);
        }
    }

    TonePick tone;
    preferTone(tone, AlarmChord::Obstacle, sObstacle.level, 2, 3);
    preferTone(tone, AlarmChord::Closure, sClosure.level, 2, 3);
    preferTone(tone, AlarmChord::Terrain, sAhead.level, 2, 3);
    preferTone(tone, AlarmChord::Sink, sSink.level, 1, 6);
    preferTone(tone, AlarmChord::TooLow, sTooLow.level, 4, 4);
    preferTone(tone, AlarmChord::DontSink, sDont.level, 7, 7);
    preferTone(tone, AlarmChord::Bank, sBank.level, 8, 8);
    AlarmTone::instance().set(tone.chord, tone.level);
    if (fiveCall && tone.level == 0)
    {
        AlarmTone::instance().sting(AlarmChord::FiveHundred);
    }
}
