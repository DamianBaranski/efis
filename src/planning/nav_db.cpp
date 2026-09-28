/// \file nav_db.cpp
/// Lazy loaders and lookups for `airports.csv`, `enroute_points.csv`, and
/// `airspaces.csv`. Files are parsed once and kept in memory for the session.
#include "nav_db.h"

#include "asset_path.h"

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
constexpr float kMPerFt = 0.3048f;

/// Splits one CSV row, honouring double-quoted values.
std::vector<std::string> parseCsvLine(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (c == '"')
        {
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
            {
                cur.push_back('"');
                ++i;
            }
            else
            {
                inQuotes = !inQuotes;
            }
        }
        else if (c == ',' && !inQuotes)
        {
            out.push_back(cur);
            cur.clear();
        }
        else
        {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string trimSpaces(std::string s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\r' || s.front() == '\t'))
    {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t'))
    {
        s.pop_back();
    }
    // Drop a UTF-8 BOM that some CSV exports put on the first header.
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF)
    {
        s.erase(0, 3);
    }
    return s;
}

int findCol(const std::vector<std::string> &headers, const std::string &name)
{
    for (size_t i = 0; i < headers.size(); ++i)
    {
        if (trimSpaces(headers[i]) == name)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}
} // namespace

NavDb &NavDb::instance()
{
    static NavDb db;
    return db;
}

std::string NavDb::upper(std::string s)
{
    for (char &c : s)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string NavDb::trim(const std::string &s)
{
    return trimSpaces(s);
}

const Waypoint *NavDb::find(const std::string &ident)
{
    ensureWaypointsLoaded();
    const std::string key = upper(trim(ident));
    const auto it = mIndex.find(key);
    if (it == mIndex.end())
    {
        return nullptr;
    }
    return &mWaypoints[it->second];
}

const std::vector<Waypoint> &NavDb::waypoints()
{
    ensureWaypointsLoaded();
    return mWaypoints;
}

std::vector<const Waypoint *> NavDb::search(const std::string &query, size_t limit)
{
    ensureWaypointsLoaded();
    std::vector<const Waypoint *> results;
    const std::string q = upper(trim(query));
    if (q.empty())
    {
        return results;
    }
    results.reserve(64);
    for (const Waypoint &wpt : mWaypoints)
    {
        const std::string identUpper = upper(wpt.ident);
        const std::string nameUpper = upper(wpt.name);
        if (identUpper.find(q) != std::string::npos || nameUpper.find(q) != std::string::npos)
        {
            results.push_back(&wpt);
        }
    }
    std::sort(results.begin(), results.end(), [&](const Waypoint *a, const Waypoint *b) {
        const bool aPrefix = upper(a->ident).rfind(q, 0) == 0;
        const bool bPrefix = upper(b->ident).rfind(q, 0) == 0;
        if (aPrefix != bPrefix)
        {
            return aPrefix;
        }
        return a->ident < b->ident;
    });
    if (results.size() > limit)
    {
        results.resize(limit);
    }
    return results;
}

const std::vector<AirspaceRing> &NavDb::airspaces()
{
    ensureAirspacesLoaded();
    return mAirspaces;
}

void NavDb::reloadAirspaces()
{
    mAirspaces.clear();
    mAirspacesLoaded = false;
    ensureAirspacesLoaded();
}

void NavDb::indexWaypoint(const Waypoint &wpt)
{
    const std::string key = upper(wpt.ident);
    if (mIndex.find(key) != mIndex.end())
    {
        return; // Airports win over enroute duplicates because they load first.
    }
    mWaypoints.push_back(wpt);
    mIndex[key] = mWaypoints.size() - 1;
}

void NavDb::ensureWaypointsLoaded()
{
    if (mWaypointsLoaded)
    {
        return;
    }
    mWaypointsLoaded = true;
    mWaypoints.reserve(24000);
    loadAirportsCsv(AssetPath::resolve("resources/airports/airports.csv"));
    loadEnrouteCsv(AssetPath::resolve("resources/navigation/enroute_points.csv"));
    std::cout << "NavDb: " << mWaypoints.size() << " waypoints loaded" << std::endl;
}

void NavDb::ensureAirspacesLoaded()
{
    if (mAirspacesLoaded)
    {
        return;
    }
    mAirspacesLoaded = true;
    loadAirspacesCsv(AssetPath::resolve("resources/airspaces/airspaces.csv"));
    std::cout << "NavDb: " << mAirspaces.size() << " airspaces loaded" << std::endl;
}

void NavDb::loadAirportsCsv(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "NavDb: cannot read " << path << std::endl;
        return;
    }
    std::string line;
    if (!std::getline(in, line))
    {
        return;
    }
    const std::vector<std::string> headers = parseCsvLine(line);
    const int icaoIdx = findCol(headers, "icao");
    const int nameIdx = findCol(headers, "name");
    const int countryIdx = findCol(headers, "country");
    const int latIdx = findCol(headers, "lat");
    const int lonIdx = findCol(headers, "lon");
    const int elevIdx = findCol(headers, "elev_m");
    if (icaoIdx < 0 || latIdx < 0 || lonIdx < 0)
    {
        std::cerr << "NavDb: airports.csv missing required columns" << std::endl;
        return;
    }
    std::unordered_set<std::string> seenIcao;
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const std::vector<std::string> cols = parseCsvLine(line);
        if (icaoIdx >= static_cast<int>(cols.size()))
        {
            continue;
        }
        const std::string icao = icaoIdx < static_cast<int>(cols.size()) ? trim(cols[icaoIdx]) : std::string();
        Waypoint wpt;
        wpt.kind = Waypoint::Kind::Airport;
        if (nameIdx >= 0 && nameIdx < static_cast<int>(cols.size()))
        {
            wpt.name = trim(cols[nameIdx]);
        }
        if (countryIdx >= 0 && countryIdx < static_cast<int>(cols.size()))
        {
            wpt.country = trim(cols[countryIdx]);
        }
        try
        {
            wpt.lat = std::stod(cols[latIdx]);
            wpt.lon = std::stod(cols[lonIdx]);
        }
        catch (...)
        {
            continue;
        }
        std::string ident = upper(icao);
        if (ident.empty())
        {
            const long latKey = static_cast<long>(std::llround(wpt.lat * 10000.0));
            const long lonKey = static_cast<long>(std::llround(wpt.lon * 10000.0));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "AF%ld%ld", latKey, lonKey);
            ident = buf;
        }
        if (!seenIcao.insert(ident).second)
        {
            continue; // one row per field (CSV is one row per runway)
        }
        wpt.ident = ident;
        if (elevIdx >= 0 && elevIdx < static_cast<int>(cols.size()) && !cols[elevIdx].empty())
        {
            try
            {
                const float elevM = std::stof(cols[elevIdx]);
                wpt.elevationFt = elevM / kMPerFt;
            }
            catch (...)
            {
            }
        }
        if (icao.empty())
        {
            mWaypoints.push_back(wpt);
        }
        else
        {
            indexWaypoint(wpt);
        }
    }
}

void NavDb::loadEnrouteCsv(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "NavDb: cannot read " << path << std::endl;
        return;
    }
    std::string line;
    if (!std::getline(in, line))
    {
        return;
    }
    const std::vector<std::string> headers = parseCsvLine(line);
    const int identIdx = findCol(headers, "ident");
    const int nameIdx = findCol(headers, "name");
    const int typeIdx = findCol(headers, "type");
    const int countryIdx = findCol(headers, "country");
    const int latIdx = findCol(headers, "lat");
    const int lonIdx = findCol(headers, "lon");
    const int elevIdx = findCol(headers, "elevation_ft");
    const int freqIdx = findCol(headers, "frequency_khz");
    if (identIdx < 0 || latIdx < 0 || lonIdx < 0)
    {
        std::cerr << "NavDb: enroute_points.csv missing required columns" << std::endl;
        return;
    }
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const std::vector<std::string> cols = parseCsvLine(line);
        if (identIdx >= static_cast<int>(cols.size()))
        {
            continue;
        }
        const std::string ident = trim(cols[identIdx]);
        if (ident.empty())
        {
            continue;
        }
        const std::string key = upper(ident);
        if (mIndex.find(key) != mIndex.end())
        {
            continue;
        }
        Waypoint wpt;
        wpt.ident = key;
        if (nameIdx >= 0 && nameIdx < static_cast<int>(cols.size()))
        {
            wpt.name = trim(cols[nameIdx]);
        }
        if (typeIdx >= 0 && typeIdx < static_cast<int>(cols.size()))
        {
            const std::string t = upper(trim(cols[typeIdx]));
            if (t == "VOR" || t == "VOR-DME" || t == "VORTAC" || t == "DME")
            {
                wpt.kind = Waypoint::Kind::Vor;
            }
            else if (t == "NDB")
            {
                wpt.kind = Waypoint::Kind::Ndb;
            }
            else if (t == "FIX" || t == "WAYPOINT")
            {
                wpt.kind = Waypoint::Kind::Fix;
            }
            else
            {
                wpt.kind = Waypoint::Kind::Other;
            }
        }
        if (countryIdx >= 0 && countryIdx < static_cast<int>(cols.size()))
        {
            wpt.country = trim(cols[countryIdx]);
        }
        try
        {
            wpt.lat = std::stod(cols[latIdx]);
            wpt.lon = std::stod(cols[lonIdx]);
        }
        catch (...)
        {
            continue;
        }
        if (elevIdx >= 0 && elevIdx < static_cast<int>(cols.size()) && !cols[elevIdx].empty())
        {
            try
            {
                wpt.elevationFt = std::stof(cols[elevIdx]);
            }
            catch (...)
            {
            }
        }
        if (freqIdx >= 0 && freqIdx < static_cast<int>(cols.size()) && !cols[freqIdx].empty())
        {
            try
            {
                const double khz = std::stod(cols[freqIdx]);
                char buf[32];
                if (khz >= 10000.0)
                {
                    std::snprintf(buf, sizeof(buf), "%.3f MHz", khz / 1000.0);
                }
                else
                {
                    std::snprintf(buf, sizeof(buf), "%.0f kHz", khz);
                }
                wpt.frequency = buf;
            }
            catch (...)
            {
            }
        }
        indexWaypoint(wpt);
    }
}

void NavDb::loadAirspacesCsv(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "NavDb: cannot read " << path << std::endl;
        return;
    }
    std::string line;
    if (!std::getline(in, line))
    {
        return;
    }
    const std::vector<std::string> headers = parseCsvLine(line);
    const int nameIdx = findCol(headers, "name");
    const int typeIdx = findCol(headers, "type");
    const int typeIdIdx = findCol(headers, "type_id");
    const int classIdx = findCol(headers, "icao_class");
    const int floorIdx = findCol(headers, "floor");
    const int ceilIdx = findCol(headers, "ceiling");
    const int minLatIdx = findCol(headers, "min_lat");
    const int maxLatIdx = findCol(headers, "max_lat");
    const int minLonIdx = findCol(headers, "min_lon");
    const int maxLonIdx = findCol(headers, "max_lon");
    const int polyIdx = findCol(headers, "polygon");
    if (nameIdx < 0 || polyIdx < 0)
    {
        std::cerr << "NavDb: airspaces.csv missing name or polygon" << std::endl;
        return;
    }
    while (std::getline(in, line))
    {
        if (line.empty())
        {
            continue;
        }
        const std::vector<std::string> cols = parseCsvLine(line);
        if (polyIdx >= static_cast<int>(cols.size()))
        {
            continue;
        }
        AirspaceRing ring;
        if (nameIdx < static_cast<int>(cols.size()))
        {
            ring.name = trim(cols[nameIdx]);
        }
        if (typeIdx >= 0 && typeIdx < static_cast<int>(cols.size()))
        {
            ring.type = trim(cols[typeIdx]);
        }
        else if (typeIdIdx >= 0 && typeIdIdx < static_cast<int>(cols.size()))
        {
            ring.type = trim(cols[typeIdIdx]);
        }
        if (classIdx >= 0 && classIdx < static_cast<int>(cols.size()))
        {
            ring.icaoClass = trim(cols[classIdx]);
        }
        if (floorIdx >= 0 && floorIdx < static_cast<int>(cols.size()))
        {
            ring.floor = trim(cols[floorIdx]);
        }
        if (ceilIdx >= 0 && ceilIdx < static_cast<int>(cols.size()))
        {
            ring.ceiling = trim(cols[ceilIdx]);
        }
        try
        {
            if (minLatIdx >= 0 && minLatIdx < static_cast<int>(cols.size()))
            {
                ring.minLat = std::stod(cols[minLatIdx]);
            }
            if (maxLatIdx >= 0 && maxLatIdx < static_cast<int>(cols.size()))
            {
                ring.maxLat = std::stod(cols[maxLatIdx]);
            }
            if (minLonIdx >= 0 && minLonIdx < static_cast<int>(cols.size()))
            {
                ring.minLon = std::stod(cols[minLonIdx]);
            }
            if (maxLonIdx >= 0 && maxLonIdx < static_cast<int>(cols.size()))
            {
                ring.maxLon = std::stod(cols[maxLonIdx]);
            }
        }
        catch (...)
        {
        }
        const std::string &poly = cols[polyIdx];
        std::stringstream pss(poly);
        std::string pair;
        while (std::getline(pss, pair, ';'))
        {
            const auto comma = pair.find(',');
            if (comma == std::string::npos)
            {
                continue;
            }
            try
            {
                const double lat = std::stod(trim(pair.substr(0, comma)));
                const double lon = std::stod(trim(pair.substr(comma + 1)));
                ring.lat.push_back(lat);
                ring.lon.push_back(lon);
            }
            catch (...)
            {
            }
        }
        if (ring.lat.size() < 3)
        {
            continue;
        }
        // Fill missing bbox from vertices.
        if (ring.minLat > 90.0)
        {
            ring.minLat = *std::min_element(ring.lat.begin(), ring.lat.end());
            ring.maxLat = *std::max_element(ring.lat.begin(), ring.lat.end());
            ring.minLon = *std::min_element(ring.lon.begin(), ring.lon.end());
            ring.maxLon = *std::max_element(ring.lon.begin(), ring.lon.end());
        }
        mAirspaces.push_back(std::move(ring));
    }
}
