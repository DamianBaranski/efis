/// \file nav_voice.cpp
/// Decides when airspace, reporting points, obstacles, and the nearest field are spoken.
#include "nav_voice.h"
#include "asset_path.h"
#include "geo_coord_utils.h"
#include "sdl_compat.h"
#include "voice_announcer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace
{
constexpr double kNm = 1852.0;
constexpr double kTwoM = 2.0 * kNm;
constexpr double kFiveM = 5.0 * kNm;
constexpr double kVrpHorizonM = 3.0 * kNm;
constexpr double kVrpOverM = 0.3 * kNm;
constexpr uint32_t kRepeatMs = 12000;

const char *kOnes[] = {"zero",     "one",     "two",       "three",    "four",
                       "five",     "six",     "seven",     "eight",    "nine",
                       "ten",      "eleven",  "twelve",    "thirteen", "fourteen",
                       "fifteen",  "sixteen", "seventeen", "eighteen", "nineteen"};
const char *kTens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

std::string speakInt(int n)
{
    if (n < 0)
    {
        return "minus " + speakInt(-n);
    }
    if (n < 20)
    {
        return kOnes[n];
    }
    if (n < 100)
    {
        if (n % 10 == 0)
        {
            return kTens[n / 10];
        }
        return std::string(kTens[n / 10]) + " " + kOnes[n % 10];
    }
    if (n < 1000)
    {
        std::string text = std::string(kOnes[n / 100]) + " hundred";
        if (n % 100 != 0)
        {
            text += " ";
            text += speakInt(n % 100);
        }
        return text;
    }
    if (n < 1000000)
    {
        std::string text = speakInt(n / 1000) + " thousand";
        if (n % 1000 != 0)
        {
            text += n % 1000 < 100 ? " and " : " ";
            text += speakInt(n % 1000);
        }
        return text;
    }
    return std::to_string(n);
}

const char *digitWord(int digit)
{
    return digit == 9 ? "niner" : kOnes[std::clamp(digit, 0, 9)];
}

std::string upperAscii(std::string text)
{
    for (char &c : text)
    {
        if (static_cast<unsigned char>(c) < 128)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return text;
}

std::string titleWord(const std::string &word)
{
    bool letter = false;
    bool allUpper = true;
    for (unsigned char c : word)
    {
        if (c >= 128)
        {
            return word;
        }
        if (std::isalpha(c))
        {
            letter = true;
            if (std::islower(c))
            {
                allUpper = false;
            }
        }
    }
    if (!letter || !allUpper)
    {
        return word;
    }
    std::string out = word;
    bool cap = true;
    for (char &c : out)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!std::isalnum(u))
        {
            cap = true;
            continue;
        }
        c = static_cast<char>(cap ? std::toupper(u) : std::tolower(u));
        cap = false;
    }
    return out;
}

const char *phonetic(char letter)
{
    switch (std::toupper(static_cast<unsigned char>(letter)))
    {
    case 'A':
        return "Alfa";
    case 'B':
        return "Bravo";
    case 'C':
        return "Charlie";
    case 'D':
        return "Delta";
    case 'E':
        return "Echo";
    case 'F':
        return "Foxtrot";
    case 'G':
        return "Golf";
    case 'H':
        return "Hotel";
    case 'I':
        return "India";
    case 'J':
        return "Juliett";
    case 'K':
        return "Kilo";
    case 'L':
        return "Lima";
    case 'M':
        return "Mike";
    case 'N':
        return "November";
    case 'O':
        return "Oscar";
    case 'P':
        return "Papa";
    case 'Q':
        return "Quebec";
    case 'R':
        return "Romeo";
    case 'S':
        return "Sierra";
    case 'T':
        return "Tango";
    case 'U':
        return "Uniform";
    case 'V':
        return "Victor";
    case 'W':
        return "Whisky";
    case 'X':
        return "X-ray";
    case 'Y':
        return "Yankee";
    case 'Z':
        return "Zulu";
    default:
        return nullptr;
    }
}

bool isIcao(const std::string &word)
{
    if (word.size() != 4)
    {
        return false;
    }
    for (unsigned char c : word)
    {
        if (!std::isalpha(c))
        {
            return false;
        }
    }
    const std::string prefix = upperAscii(word.substr(0, 2));
    static const std::unordered_set<std::string> kPrefixes{"LK", "EP", "LZ", "LO", "ED", "LH", "EB", "LS", "LI",
                                                           "LF", "EH", "EK", "EF", "ES", "EN", "BI", "EG", "UK"};
    return kPrefixes.count(prefix) != 0;
}

std::string spellIcao(const std::string &word)
{
    std::string out;
    for (char c : word)
    {
        const char *said = phonetic(c);
        if (!said)
        {
            continue;
        }
        if (!out.empty())
        {
            out += ' ';
        }
        out += said;
    }
    return out;
}

bool isClassToken(const std::string &word)
{
    const std::string upper = upperAscii(word);
    return upper == "CTR" || upper == "TMA" || upper == "ATZ" || upper == "RMZ" || upper == "TMZ" || upper == "MATZ" ||
           upper == "CTA";
}

std::string speakPlace(const std::string &raw)
{
    std::istringstream in(raw);
    std::string token;
    std::string out;
    while (in >> token)
    {
        if (isClassToken(token))
        {
            continue;
        }
        const std::string piece = isIcao(token) ? spellIcao(token) : titleWord(token);
        if (piece.empty())
        {
            continue;
        }
        if (!out.empty())
        {
            out += ' ';
        }
        out += piece;
    }
    return out;
}

const char *classPhrase(int type)
{
    switch (type)
    {
    case 1:
        return "restricted area";
    case 2:
        return "danger area";
    case 3:
        return "prohibited area";
    case 4:
        return "control zone";
    case 5:
        return "T M Z";
    case 6:
        return "R M Z";
    case 7:
        return "terminal area";
    case 13:
        return "A T Z";
    case 14:
        return "M A T Z";
    default:
        return "airspace";
    }
}

std::string spokenVolume(const std::string &name, int type)
{
    const std::string place = speakPlace(name);
    const char *kind = classPhrase(type);
    if (place.empty())
    {
        return kind;
    }
    return place + ", " + kind;
}

std::string speakLimit(const std::string &label)
{
    if (label == "GND")
    {
        return "ground";
    }
    if (label.rfind("FL", 0) == 0)
    {
        const int level = std::atoi(label.c_str() + 2);
        if (level >= 0 && level < 100)
        {
            return std::string("flight level ") + digitWord(level / 10) + " " + digitWord(level % 10);
        }
        return "flight level " + speakInt(level);
    }
    int value = 0;
    char unit[8] = {};
    char extra[8] = {};
    const int got = std::sscanf(label.c_str(), "%d%7s %7s", &value, unit, extra);
    if (got < 2)
    {
        return label;
    }
    const bool agl = got >= 3 && std::string(extra) == "AGL";
    std::string text = speakInt(value);
    if (std::string(unit) == "ft")
    {
        text += " feet";
    }
    else if (std::string(unit) == "m")
    {
        text += " metres";
    }
    else
    {
        return label;
    }
    if (agl)
    {
        text += " above ground";
    }
    return text;
}

std::string speakRunway(const std::string &designator)
{
    std::string out;
    for (char c : designator)
    {
        if (c >= '0' && c <= '9')
        {
            if (!out.empty())
            {
                out += ' ';
            }
            out += digitWord(c - '0');
        }
        else if (c == 'L' || c == 'l')
        {
            out += " left";
        }
        else if (c == 'R' || c == 'r')
        {
            out += " right";
        }
        else if (c == 'C' || c == 'c')
        {
            out += " centre";
        }
    }
    return out;
}

std::string speakMiles(double meters)
{
    const double nm = meters / kNm;
    if (nm < 0.5)
    {
        return "under one mile";
    }
    const int rounded = std::max(1, static_cast<int>(std::lround(nm)));
    if (rounded == 1)
    {
        return "one mile";
    }
    return speakInt(rounded) + " miles";
}

bool zoneType(int type)
{
    return type == 4 || type == 5 || type == 6 || type == 7 || type == 13 || type == 14;
}

std::string vrpKey(const VrpOverlay::Point &point)
{
    std::ostringstream out;
    out << point.name << '@' << static_cast<int>(std::lround(point.latitude * 10000.0)) << ','
        << static_cast<int>(std::lround(point.longitude * 10000.0));
    return out.str();
}

constexpr int kObstWind = 0;
constexpr int kObstChimney = 1;
constexpr int kObstTower = 2;
constexpr int kObstBuilding = 3;
constexpr double kObstHorizonM = 3.0 * kNm;
constexpr double kFarmMeters = 1500.0;

bool obstacleWorth(const NavVoice::ObstacleCue &point)
{
    if (point.kind == kObstWind || point.kind == kObstChimney || point.kind == kObstTower)
    {
        return true;
    }
    return point.heightM >= 50.0f;
}

const char *obstacleKind(int kind, bool plural)
{
    switch (kind)
    {
    case kObstWind:
        return plural ? "Wind turbines" : "Wind turbine";
    case kObstChimney:
        return "Chimney";
    case kObstTower:
        return "Tower";
    case kObstBuilding:
        return "Building";
    default:
        return "Obstacle";
    }
}

std::string obstacleKey(const NavVoice::ObstacleCue &point, bool farm)
{
    std::ostringstream out;
    if (farm)
    {
        out << "farm@" << static_cast<int>(std::lround(point.latitude / 0.02)) << ','
            << static_cast<int>(std::lround(point.longitude / 0.02));
        return out.str();
    }
    out << point.kind << '@' << static_cast<int>(std::lround(point.latitude * 10000.0)) << ','
        << static_cast<int>(std::lround(point.longitude * 10000.0));
    return out.str();
}

std::string obstaclePhrase(const NavVoice::ObstacleCue &point, bool farm)
{
    std::string text = obstacleKind(point.kind, farm);
    if (!farm && !point.name.empty())
    {
        const std::string place = speakPlace(point.name);
        if (!place.empty())
        {
            text += ", ";
            text += place;
        }
    }
    if (!farm && point.heightM > 1.0f)
    {
        text += ", ";
        text += speakInt(static_cast<int>(std::lround(point.heightM)));
        text += " metres";
    }
    return text;
}
} // namespace

NavVoice &NavVoice::instance()
{
    static NavVoice voice;
    return voice;
}

void NavVoice::setPosition(double latitude, double longitude)
{
    mLatitude = latitude;
    mLongitude = longitude;
    mHavePosition = true;
}

void NavVoice::preview()
{
    VoiceAnnouncer::instance().say("Navigation voice ready.", 1);
}

void NavVoice::stepVoice(int delta)
{
    VoiceAnnouncer::instance().stepVoice(delta);
}

std::string NavVoice::voiceLabel()
{
    return VoiceAnnouncer::instance().voiceLabel();
}

void NavVoice::say(const std::string &key, const std::string &text, int priority, bool channel)
{
    if (!mNarration || !channel)
    {
        return;
    }
    const uint32_t now = SDL_GetTicks();
    for (auto &row : mRecent)
    {
        if (row.first == key)
        {
            if (now - row.second < kRepeatMs)
            {
                return;
            }
            row.second = now;
            VoiceAnnouncer::instance().say(text, priority);
            return;
        }
    }
    mRecent.emplace_back(key, now);
    VoiceAnnouncer::instance().say(text, priority);
}

void NavVoice::noteAirspace(const std::string &name, int type, const std::string &lowerLabel, double distM,
                            bool horizontal, int vertical)
{
    const std::string id = name + "|" + std::to_string(type) + "|" + lowerLabel;
    AirMem *mem = nullptr;
    for (auto &row : mAir)
    {
        if (row.first == id)
        {
            mem = &row.second;
            break;
        }
    }
    if (!mem)
    {
        mAir.emplace_back(id, AirMem{});
        mem = &mAir.back().second;
    }

    const bool inside = horizontal && vertical == 0;
    const bool under = horizontal && vertical < 0;
    const bool over = horizontal && vertical > 0;
    const std::string volume = spokenVolume(name, type);
    const int alert = (type == 1 || type == 2 || type == 3) ? 1 : 0;

    if (!mem->known)
    {
        mem->known = true;
        mem->inside = inside;
        mem->under = under;
        mem->over = over;
        mem->arm2 = distM > kTwoM;
        mem->arm5 = distM > kFiveM;
        mem->lastDistM = distM;
        return;
    }

    if (inside && !mem->inside)
    {
        say("enter:" + id, "Entering " + volume + ".", alert, mAirspace);
        mem->arm2 = false;
    }
    else if (!horizontal && mem->inside)
    {
        say("leave:" + id, "Leaving " + volume + ".", 0, mAirspace);
    }
    if (under && !mem->under)
    {
        std::string text = volume + " below.";
        if (lowerLabel != "GND" && !lowerLabel.empty())
        {
            text += " Floor " + speakLimit(lowerLabel) + ".";
        }
        say("below:" + id, text, 0, mAirspace);
    }
    if (over && !mem->over)
    {
        say("above:" + id, volume + " above.", 0, mAirspace);
    }

    if (type == 3 && !horizontal && mem->arm5 && mem->lastDistM > kFiveM && distM <= kFiveM)
    {
        say("ahead:" + id, "Prohibited area ahead.", 1, mAirspace);
        mem->arm5 = false;
    }
    else if (type != 3 && !horizontal && vertical == 0 && mem->arm2 && mem->lastDistM > kTwoM && distM <= kTwoM)
    {
        if (type == 1)
        {
            say("close:" + id, "Restricted area, two miles.", 1, mAirspace);
        }
        else if (type == 2)
        {
            say("close:" + id, "Danger area, two miles.", 1, mAirspace);
        }
        else if (zoneType(type))
        {
            say("close:" + id, volume + ", two miles, inbound.", 0, mAirspace);
        }
        mem->arm2 = false;
    }

    if (distM > kTwoM + 0.4 * kNm)
    {
        mem->arm2 = true;
    }
    if (distM > kFiveM + 0.5 * kNm)
    {
        mem->arm5 = true;
    }
    mem->inside = inside;
    mem->under = under;
    mem->over = over;
    mem->lastDistM = distM;
}

void NavVoice::updateReporting(double latitude, double longitude, const std::vector<VrpOverlay::Point> &points)
{
    setPosition(latitude, longitude);
    const VrpOverlay::Point *best = nullptr;
    double bestD = kVrpHorizonM;
    for (const auto &point : points)
    {
        if (!point.compulsory || point.name.empty())
        {
            continue;
        }
        const double dist = GeoCoordUtils::calculateDistance(latitude, longitude, point.latitude, point.longitude);
        if (dist < bestD)
        {
            bestD = dist;
            best = &point;
        }
    }

    if (!mVrpPrimed)
    {
        mVrpPrimed = true;
        if (best)
        {
            mVrpKey = vrpKey(*best);
            mVrpTwo = bestD <= kTwoM;
            mVrpOver = bestD <= kVrpOverM;
        }
        return;
    }

    if (!best)
    {
        mVrpKey.clear();
        mVrpTwo = false;
        mVrpOver = false;
        return;
    }

    const std::string key = vrpKey(*best);
    const std::string nice = speakPlace(best->name);
    if (key != mVrpKey)
    {
        mVrpKey = key;
        mVrpTwo = false;
        mVrpOver = false;
    }
    if (!mVrpOver && bestD <= kVrpOverM)
    {
        say("vrp-over:" + key, "Over " + nice + ".", 0, mReporting);
        mVrpOver = true;
        mVrpTwo = true;
    }
    else if (!mVrpTwo && bestD <= kTwoM)
    {
        say("vrp-two:" + key, "Reporting point " + nice + ", two miles.", 0, mReporting);
        mVrpTwo = true;
    }
    if (bestD > kTwoM + 0.4 * kNm)
    {
        mVrpTwo = false;
    }
    if (bestD > kVrpOverM + 0.15 * kNm)
    {
        mVrpOver = false;
    }
}

void NavVoice::updateObstacles(double latitude, double longitude, const std::vector<ObstacleCue> &points)
{
    setPosition(latitude, longitude);
    const ObstacleCue *best = nullptr;
    double bestD = kObstHorizonM;
    int farmCount = 0;
    for (const auto &point : points)
    {
        if (!obstacleWorth(point))
        {
            continue;
        }
        const double dist = GeoCoordUtils::calculateDistance(latitude, longitude, point.latitude, point.longitude);
        if (dist < bestD)
        {
            bestD = dist;
            best = &point;
        }
    }
    if (best && best->kind == kObstWind)
    {
        for (const auto &point : points)
        {
            if (point.kind != kObstWind)
            {
                continue;
            }
            const double dist =
                GeoCoordUtils::calculateDistance(best->latitude, best->longitude, point.latitude, point.longitude);
            if (dist <= kFarmMeters)
            {
                ++farmCount;
            }
        }
    }
    const bool farm = farmCount >= 3;

    if (!mObstPrimed)
    {
        mObstPrimed = true;
        if (best)
        {
            mObstKey = obstacleKey(*best, farm);
            mObstTwo = bestD <= kTwoM;
            mObstOver = best->kind != kObstWind && bestD <= kVrpOverM;
        }
        return;
    }

    if (!best)
    {
        mObstKey.clear();
        mObstTwo = false;
        mObstOver = false;
        return;
    }

    const std::string key = obstacleKey(*best, farm);
    const std::string words = obstaclePhrase(*best, farm);
    if (key != mObstKey)
    {
        mObstKey = key;
        mObstTwo = false;
        mObstOver = false;
    }
    if (best->kind != kObstWind && !mObstOver && bestD <= kVrpOverM)
    {
        say("obst-over:" + key, "Over " + words + ".", 0, mObstacles);
        mObstOver = true;
        mObstTwo = true;
    }
    else if (!mObstTwo && bestD <= kTwoM)
    {
        say("obst-two:" + key, words + ", two miles.", 0, mObstacles);
        mObstTwo = true;
    }
    if (bestD > kTwoM + 0.4 * kNm)
    {
        mObstTwo = false;
    }
    if (bestD > kVrpOverM + 0.15 * kNm)
    {
        mObstOver = false;
    }
}

void NavVoice::announceNearest()
{
    if (!mHavePosition || !mNarration || !mNearest)
    {
        return;
    }
    if (!mAirportsLoaded)
    {
        mAirportsLoaded = true;
        std::ifstream in(AssetPath::resolve("resources/airports/airports.csv"));
        std::string line;
        std::getline(in, line);
        while (std::getline(in, line))
        {
            if (line.empty())
            {
                continue;
            }
            std::vector<std::string> fields;
            std::string field;
            std::istringstream row(line);
            while (std::getline(row, field, ','))
            {
                fields.push_back(field);
            }
            if (fields.size() < 10)
            {
                continue;
            }
            Field item;
            item.icao = upperAscii(fields[0]);
            item.name = fields[1];
            item.latitude = std::atof(fields[4].c_str());
            item.longitude = std::atof(fields[5].c_str());
            item.runway = fields[7];
            item.lengthM = std::strtof(fields[9].c_str(), nullptr);
            auto it = std::find_if(mFields.begin(), mFields.end(),
                                   [&](const Field &have) { return have.icao == item.icao; });
            if (it == mFields.end())
            {
                mFields.push_back(std::move(item));
            }
            else if (item.lengthM > it->lengthM)
            {
                it->runway = item.runway;
                it->lengthM = item.lengthM;
            }
        }
    }
    if (mFields.empty())
    {
        return;
    }

    const Field *best = nullptr;
    double bestD = 1.0e12;
    for (const auto &field : mFields)
    {
        const double dist = GeoCoordUtils::calculateDistance(mLatitude, mLongitude, field.latitude, field.longitude);
        if (dist < bestD)
        {
            bestD = dist;
            best = &field;
        }
    }
    if (!best)
    {
        return;
    }
    const std::string ident = isIcao(best->icao) ? spellIcao(best->icao) : speakPlace(best->icao);
    std::string text = "Nearest, " + ident;
    const std::string place = speakPlace(best->name);
    if (!place.empty())
    {
        text += ", ";
        text += place;
    }
    text += ". Runway ";
    text += speakRunway(best->runway);
    text += ", ";
    text += speakMiles(bestD);
    text += ".";
    VoiceAnnouncer::instance().say(text, 0);
}
