/// \file airspace_overlay.cpp
/// Builds airspace walls and name plates from the OpenAIP GeoJSON.
#include "airspace_overlay.h"
#include "asset_path.h"
#include "geo_coord_utils.h"
#include "nav_voice.h"
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <GLES3/gl3.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
constexpr double kNearbyMeters = 100000.0;
constexpr double kRebuildMeters = 15000.0;
constexpr float kDefaultGroundM = 151.0f;
constexpr float kGndBiasM = 2.0f;
constexpr float kMaxFloorAglM = 3000.0f;
constexpr float kMaxCeilingM = 12000.0f;
constexpr float kMinThicknessM = 10.0f;
constexpr int kMaxRingVerts = 80;
constexpr double kMetersPerNm = 1852.0;
constexpr double kFollowTauSec = 3.2;
constexpr double kFollowMaxMps = 70.0;
constexpr double kRedrawAlongM = 90.0;
constexpr float kAhrsHalfW = 240.0f;
constexpr float kAhrsHalfH = 260.0f;
constexpr char kAirspaceRel[] = "resources/airspaces";
constexpr char kAirportCsvRel[] = "resources/airports/airports.csv";
constexpr char kFontRel[] = "resources/fonts/B612Mono-Regular.ttf";

const std::unordered_set<int> kDrawTypes{1, 2, 3, 4, 5, 6, 7, 13, 14};

uint32_t colorForType(int type)
{
    switch (type)
    {
    case 1:
        return 0xC43C3C88;
    case 2:
        return 0xD8782088;
    case 3:
        return 0xE0184888;
    case 4:
        return 0x2E78C088;
    case 5:
        return 0x70B04088;
    case 6:
        return 0xD0A02888;
    case 7:
        return 0x28A8C888;
    case 13:
    case 14:
        return 0x5AA0D888;
    default:
        return 0x80808088;
    }
}

int jsonInt(const nlohmann::json &obj, const char *key, int fallback = 0)
{
    if (!obj.contains(key) || obj[key].is_null())
    {
        return fallback;
    }
    if (obj[key].is_number_integer())
    {
        return obj[key].get<int>();
    }
    if (obj[key].is_number())
    {
        return static_cast<int>(obj[key].get<double>());
    }
    return fallback;
}

float jsonFloat(const nlohmann::json &obj, const char *key, float fallback = 0.0f)
{
    if (!obj.contains(key) || obj[key].is_null() || !obj[key].is_number())
    {
        return fallback;
    }
    return obj[key].get<float>();
}

float limitToAmslM(const nlohmann::json &limit, float groundAmslM)
{
    if (!limit.is_object())
    {
        return groundAmslM;
    }
    const float value = jsonFloat(limit, "value");
    const int unit = jsonInt(limit, "unit", 1);
    const int datum = jsonInt(limit, "referenceDatum", 1);
    float meters = 0.0f;
    if (unit == 6 || datum == 2)
    {
        return value * 100.0f * 0.3048f;
    }
    if (unit == 0)
    {
        meters = value;
    }
    else
    {
        meters = value * 0.3048f;
    }
    if (datum == 0)
    {
        return groundAmslM + meters;
    }
    return meters;
}

std::string formatLimit(const nlohmann::json &limit)
{
    if (!limit.is_object())
    {
        return "?";
    }
    const float value = jsonFloat(limit, "value");
    const int unit = jsonInt(limit, "unit", 1);
    const int datum = jsonInt(limit, "referenceDatum", 1);
    const int rounded = static_cast<int>(std::lround(value));
    if (unit == 6 || datum == 2)
    {
        return "FL" + std::to_string(rounded);
    }
    if (datum == 0 && value <= 0.5f)
    {
        return "GND";
    }
    const std::string num = std::to_string(rounded);
    if (datum == 0)
    {
        return unit == 0 ? num + "m AGL" : num + "ft AGL";
    }
    return unit == 0 ? num + "m" : num + "ft";
}

std::string labelMaterial(const std::string &label)
{
    return std::string("asp:") + label;
}

VertexTexture makeVertex(const glm::dvec3 &center, double lat, double lon, float alt, float u = 0.0f, float v = 0.0f)
{
    const auto xyz = GeoCoordUtils::convertLatLonToXYZ(lat, lon, alt);
    VertexTexture vt{};
    vt.vertex.x = static_cast<float>(xyz.x - center.x);
    vt.vertex.y = static_cast<float>(xyz.y - center.y);
    vt.vertex.z = static_cast<float>(xyz.z - center.z);
    vt.textureCoord.x = u;
    vt.textureCoord.y = v;
    return vt;
}

std::vector<AirspaceOverlay::Point> openRing(std::vector<AirspaceOverlay::Point> ring)
{
    while (ring.size() >= 2)
    {
        const auto &a = ring.front();
        const auto &b = ring.back();
        if (std::abs(a.lat - b.lat) > 1e-9 || std::abs(a.lon - b.lon) > 1e-9)
        {
            break;
        }
        ring.pop_back();
    }
    return ring;
}

std::vector<AirspaceOverlay::Point> decimateRing(const std::vector<AirspaceOverlay::Point> &ring)
{
    if (ring.size() <= static_cast<size_t>(kMaxRingVerts))
    {
        return ring;
    }
    std::vector<AirspaceOverlay::Point> out;
    out.reserve(static_cast<size_t>(kMaxRingVerts));
    const double step = static_cast<double>(ring.size()) / static_cast<double>(kMaxRingVerts);
    for (int i = 0; i < kMaxRingVerts; ++i)
    {
        out.push_back(ring[static_cast<size_t>(i * step)]);
    }
    return out;
}

bool pointInPolygon(double lat, double lon, const std::vector<AirspaceOverlay::Point> &ring)
{
    bool inside = false;
    const size_t n = ring.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++)
    {
        const double yi = ring[i].lat;
        const double yj = ring[j].lat;
        const double xi = ring[i].lon;
        const double xj = ring[j].lon;
        const bool intersect = ((yi > lat) != (yj > lat)) &&
                               (lon < (xj - xi) * (lat - yi) / ((yj - yi) + 1e-15) + xi);
        if (intersect)
        {
            inside = !inside;
        }
    }
    return inside;
}

void localNE(double lat0, double lon0, double lat1, double lon1, double &northM, double &eastM)
{
    const double metersPerDegLat = 111320.0;
    const double metersPerDegLon = std::max(1000.0, metersPerDegLat * std::cos(glm::radians(lat0)));
    northM = (lat1 - lat0) * metersPerDegLat;
    eastM = (lon1 - lon0) * metersPerDegLon;
}

double distPointToSegmentM(double lat, double lon, const AirspaceOverlay::Point &a, const AirspaceOverlay::Point &b)
{
    double aN = 0.0;
    double aE = 0.0;
    double bN = 0.0;
    double bE = 0.0;
    localNE(lat, lon, a.lat, a.lon, aN, aE);
    localNE(lat, lon, b.lat, b.lon, bN, bE);
    const double abN = bN - aN;
    const double abE = bE - aE;
    const double ab2 = abN * abN + abE * abE;
    if (ab2 < 1.0)
    {
        return std::hypot(aN, aE);
    }
    const double t = std::clamp(-(aN * abN + aE * abE) / ab2, 0.0, 1.0);
    return std::hypot(aN + t * abN, aE + t * abE);
}

double distToRingM(double lat, double lon, const std::vector<AirspaceOverlay::Point> &ring)
{
    if (ring.size() < 2)
    {
        return 1.0e9;
    }
    double best = 1.0e9;
    for (size_t i = 0; i < ring.size(); ++i)
    {
        best = std::min(best, distPointToSegmentM(lat, lon, ring[i], ring[(i + 1) % ring.size()]));
    }
    return best;
}

double ringPerimeterM(const std::vector<AirspaceOverlay::Point> &ring)
{
    double length = 0.0;
    for (size_t i = 0; i < ring.size(); ++i)
    {
        const auto &a = ring[i];
        const auto &b = ring[(i + 1) % ring.size()];
        length += GeoCoordUtils::calculateDistance(a.lat, a.lon, b.lat, b.lon);
    }
    return length;
}

double wrapArcDelta(double delta, double perimeter)
{
    if (perimeter <= 0.0)
    {
        return 0.0;
    }
    while (delta > perimeter * 0.5)
    {
        delta -= perimeter;
    }
    while (delta < -perimeter * 0.5)
    {
        delta += perimeter;
    }
    return delta;
}

double nearestSOnRing(double lat, double lon, const std::vector<AirspaceOverlay::Point> &ring)
{
    if (ring.size() < 2)
    {
        return 0.0;
    }
    double acc = 0.0;
    double bestS = 0.0;
    double bestD = 1.0e12;
    for (size_t i = 0; i < ring.size(); ++i)
    {
        const auto &a = ring[i];
        const auto &b = ring[(i + 1) % ring.size()];
        const double edgeLen = GeoCoordUtils::calculateDistance(a.lat, a.lon, b.lat, b.lon);
        double aN = 0.0;
        double aE = 0.0;
        double bN = 0.0;
        double bE = 0.0;
        localNE(lat, lon, a.lat, a.lon, aN, aE);
        localNE(lat, lon, b.lat, b.lon, bN, bE);
        const double abN = bN - aN;
        const double abE = bE - aE;
        const double ab2 = abN * abN + abE * abE;
        double t = 0.0;
        double dist = std::hypot(aN, aE);
        if (ab2 >= 1.0)
        {
            t = std::clamp(-(aN * abN + aE * abE) / ab2, 0.0, 1.0);
            dist = std::hypot(aN + t * abN, aE + t * abE);
        }
        if (dist < bestD)
        {
            bestD = dist;
            bestS = acc + t * edgeLen;
        }
        acc += edgeLen;
    }
    return bestS;
}

struct RingSample
{
    double lat = 0.0;
    double lon = 0.0;
    float northHat = 1.0f;
    float eastHat = 0.0f;
    double edgeLen = 1.0;
};

RingSample sampleRingAtS(const std::vector<AirspaceOverlay::Point> &ring, double s)
{
    RingSample sample;
    if (ring.size() < 2)
    {
        return sample;
    }
    const double perimeter = ringPerimeterM(ring);
    if (perimeter <= 1.0)
    {
        sample.lat = ring.front().lat;
        sample.lon = ring.front().lon;
        return sample;
    }
    s = std::fmod(s, perimeter);
    if (s < 0.0)
    {
        s += perimeter;
    }
    double acc = 0.0;
    for (size_t i = 0; i < ring.size(); ++i)
    {
        const auto &a = ring[i];
        const auto &b = ring[(i + 1) % ring.size()];
        const double edgeLen = GeoCoordUtils::calculateDistance(a.lat, a.lon, b.lat, b.lon);
        if (edgeLen < 0.5)
        {
            continue;
        }
        if (acc + edgeLen >= s || i + 1 == ring.size())
        {
            const double t = std::clamp((s - acc) / edgeLen, 0.0, 1.0);
            sample.lat = a.lat + (b.lat - a.lat) * t;
            sample.lon = a.lon + (b.lon - a.lon) * t;
            sample.edgeLen = edgeLen;
            double n = 0.0;
            double e = 0.0;
            localNE(a.lat, a.lon, b.lat, b.lon, n, e);
            const double len = std::hypot(n, e);
            if (len > 1.0)
            {
                sample.northHat = static_cast<float>(n / len);
                sample.eastHat = static_cast<float>(e / len);
            }
            return sample;
        }
        acc += edgeLen;
    }
    sample.lat = ring.front().lat;
    sample.lon = ring.front().lon;
    return sample;
}

bool projectToScreen(const glm::dvec3 &center, const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward,
                     const glm::vec3 &up, double lat, double lon, float alt, int screenW, int screenH, float &sx,
                     float &sy)
{
    const auto xyz = GeoCoordUtils::convertLatLonToXYZ(lat, lon, alt);
    const glm::vec3 local(static_cast<float>(xyz.x - center.x), static_cast<float>(xyz.y - center.y),
                          static_cast<float>(xyz.z - center.z));
    const glm::vec3 eyeLocal(eye - center);
    const glm::mat4 view = glm::lookAt(eyeLocal, eyeLocal + forward, up);
    const glm::vec4 clip = proj * view * glm::vec4(local, 1.0f);
    if (clip.w <= 0.05f)
    {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f)
    {
        return false;
    }
    sx = (ndc.x * 0.5f + 0.5f) * static_cast<float>(screenW);
    sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(screenH);
    return true;
}

bool inAhrsKeepout(float sx, float sy, int screenW, int screenH)
{
    const float cx = 0.5f * static_cast<float>(screenW);
    const float cy = 0.5f * static_cast<float>(screenH);
    return std::fabs(sx - cx) < kAhrsHalfW && std::fabs(sy - cy) < kAhrsHalfH;
}

double sOutsideAhrs(const AirspaceOverlay::Volume &volume, double nearestS, const glm::dvec3 &center,
                    const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up,
                    int screenW, int screenH)
{
    const double perimeter = ringPerimeterM(volume.ring);
    if (perimeter <= 1.0)
    {
        return nearestS;
    }
    const float midAlt = 0.5f * (volume.lowerM + volume.upperM);
    const int samples = std::clamp(static_cast<int>(perimeter / 70.0), 16, 72);
    double bestOutside = nearestS;
    double bestOutsideCost = 1.0e12;
    bool foundOutside = false;
    double bestFallback = nearestS;
    double bestFallbackScore = -1.0;
    for (int i = 0; i < samples; ++i)
    {
        const double s = (static_cast<double>(i) / static_cast<double>(samples)) * perimeter;
        const RingSample sample = sampleRingAtS(volume.ring, s);
        float sx = 0.0f;
        float sy = 0.0f;
        if (!projectToScreen(center, proj, eye, forward, up, sample.lat, sample.lon, midAlt, screenW, screenH, sx, sy))
        {
            continue;
        }
        const double alongCost = std::fabs(wrapArcDelta(s - nearestS, perimeter));
        const float cx = 0.5f * static_cast<float>(screenW);
        const float cy = 0.5f * static_cast<float>(screenH);
        const float edgeScore = std::fabs(sx - cx) / kAhrsHalfW + std::fabs(sy - cy) / kAhrsHalfH;
        if (edgeScore > bestFallbackScore)
        {
            bestFallbackScore = edgeScore;
            bestFallback = s;
        }
        if (inAhrsKeepout(sx, sy, screenW, screenH))
        {
            continue;
        }
        if (alongCost < bestOutsideCost)
        {
            bestOutsideCost = alongCost;
            bestOutside = s;
            foundOutside = true;
        }
    }
    return foundOutside ? bestOutside : bestFallback;
}

std::vector<AirspaceOverlay::Point> parseRing(const nlohmann::json &coords)
{
    std::vector<AirspaceOverlay::Point> ring;
    if (!coords.is_array() || coords.empty())
    {
        return ring;
    }
    ring.reserve(coords.size());
    for (const auto &pt : coords)
    {
        if (!pt.is_array() || pt.size() < 2 || !pt[0].is_number() || !pt[1].is_number())
        {
            continue;
        }
        ring.push_back({pt[1].get<double>(), pt[0].get<double>()});
    }
    return openRing(std::move(ring));
}

std::vector<std::vector<AirspaceOverlay::Point>> parsePolygons(const nlohmann::json &geometry)
{
    std::vector<std::vector<AirspaceOverlay::Point>> rings;
    if (!geometry.is_object())
    {
        return rings;
    }
    const std::string type = geometry.value("type", "");
    const auto &coords = geometry["coordinates"];
    if (type == "Polygon" && coords.is_array() && !coords.empty())
    {
        auto ring = parseRing(coords[0]);
        if (ring.size() >= 3)
        {
            rings.push_back(std::move(ring));
        }
    }
    else if (type == "MultiPolygon" && coords.is_array())
    {
        for (const auto &poly : coords)
        {
            if (!poly.is_array() || poly.empty())
            {
                continue;
            }
            auto ring = parseRing(poly[0]);
            if (ring.size() >= 3)
            {
                rings.push_back(std::move(ring));
            }
        }
    }
    return rings;
}

void fillCentroid(AirspaceOverlay::Volume &volume)
{
    volume.minLat = volume.maxLat = volume.ring.front().lat;
    volume.minLon = volume.maxLon = volume.ring.front().lon;
    double lat = 0.0;
    double lon = 0.0;
    for (const auto &p : volume.ring)
    {
        lat += p.lat;
        lon += p.lon;
        volume.minLat = std::min(volume.minLat, p.lat);
        volume.maxLat = std::max(volume.maxLat, p.lat);
        volume.minLon = std::min(volume.minLon, p.lon);
        volume.maxLon = std::max(volume.maxLon, p.lon);
    }
    const double n = static_cast<double>(volume.ring.size());
    volume.centLat = lat / n;
    volume.centLon = lon / n;
}
} // namespace

AirspaceOverlay::AirspaceOverlay()
{
    loadAirportElev();
    loadCatalog();
}

void AirspaceOverlay::loadAirportElev()
{
    std::ifstream file(AssetPath::resolve(kAirportCsvRel));
    if (!file)
    {
        return;
    }
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }
        std::vector<std::string> fields;
        std::string field;
        std::istringstream in(line);
        while (std::getline(in, field, ','))
        {
            fields.push_back(field);
        }
        if (fields.size() < 7)
        {
            continue;
        }
        AirportElev ap;
        try
        {
            ap.lat = std::stod(fields[4]);
            ap.lon = std::stod(fields[5]);
            ap.elevM = fields[6].empty() ? kDefaultGroundM : std::stof(fields[6]);
        }
        catch (const std::exception &)
        {
            continue;
        }
        mAirports.push_back(ap);
    }
}

float AirspaceOverlay::nearestGroundM(double lat, double lon) const
{
    if (mAirports.empty())
    {
        return kDefaultGroundM;
    }
    float best = kDefaultGroundM;
    double bestD = 1.0e12;
    for (const auto &ap : mAirports)
    {
        const double d = GeoCoordUtils::calculateDistance(lat, lon, ap.lat, ap.lon);
        if (d < bestD)
        {
            bestD = d;
            best = ap.elevM;
        }
    }
    return best;
}

void AirspaceOverlay::loadCatalog()
{
    namespace fs = std::filesystem;
    const fs::path dir(AssetPath::resolve(kAirspaceRel));
    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        std::cerr << "Airspace overlay: missing " << dir << std::endl;
        return;
    }
    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() == ".geojson")
        {
            loadGeoJson(entry.path().string());
        }
    }
    dropDuplicateRmz();
    std::cout << "Airspace overlay loaded " << mCatalog.size() << " volumes from " << dir << std::endl;
}

void AirspaceOverlay::loadGeoJson(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "Airspace overlay: cannot read " << path << std::endl;
        return;
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.contains("features") || !doc["features"].is_array())
    {
        std::cerr << "Airspace overlay: bad GeoJSON " << path << std::endl;
        return;
    }
    for (const auto &feature : doc["features"])
    {
        const auto &props = feature["properties"];
        if (!props.is_object())
        {
            continue;
        }
        const int type = jsonInt(props, "type");
        if (!kDrawTypes.count(type))
        {
            continue;
        }
        auto rings = parsePolygons(feature.value("geometry", nlohmann::json::object()));
        const std::string name = props.value("name", std::string("airspace"));
        const nlohmann::json lower = props.value("lowerLimit", nlohmann::json::object());
        const nlohmann::json upper = props.value("upperLimit", nlohmann::json::object());
        for (auto &ring : rings)
        {
            Volume volume;
            volume.name = name;
            volume.type = type;
            volume.ring = std::move(ring);
            fillCentroid(volume);
            const float ground = nearestGroundM(volume.centLat, volume.centLon);
            volume.lowerM = limitToAmslM(lower, ground);
            volume.upperM = limitToAmslM(upper, ground);
            volume.lowerLabel = formatLimit(lower);
            volume.upperLabel = formatLimit(upper);
            if (jsonInt(lower, "referenceDatum") == 0)
            {
                volume.lowerM = std::max(volume.lowerM, ground + kGndBiasM);
            }
            if (volume.upperM <= volume.lowerM + kMinThicknessM)
            {
                continue;
            }
            if (volume.upperM > kMaxCeilingM)
            {
                continue;
            }
            if (volume.lowerM - ground > kMaxFloorAglM)
            {
                continue;
            }
            mCatalog.push_back(std::move(volume));
        }
    }
}

void AirspaceOverlay::dropDuplicateRmz()
{
    std::vector<Volume> kept;
    kept.reserve(mCatalog.size());
    for (const auto &a : mCatalog)
    {
        if (a.type != 6)
        {
            kept.push_back(a);
            continue;
        }
        bool dup = false;
        for (const auto &b : mCatalog)
        {
            if (b.type != 4 && b.type != 7 && b.type != 13)
            {
                continue;
            }
            if (GeoCoordUtils::calculateDistance(a.centLat, a.centLon, b.centLat, b.centLon) > 800.0)
            {
                continue;
            }
            if (std::abs(a.lowerM - b.lowerM) > 40.0f || std::abs(a.upperM - b.upperM) > 40.0f)
            {
                continue;
            }
            dup = true;
            break;
        }
        if (!dup)
        {
            kept.push_back(a);
        }
    }
    mCatalog.swap(kept);
}

bool AirspaceOverlay::isNearby(const Volume &volume, double latitude, double longitude) const
{
    if (GeoCoordUtils::calculateDistance(latitude, longitude, volume.centLat, volume.centLon) <= kNearbyMeters)
    {
        return true;
    }
    return latitude >= volume.minLat - 0.3 && latitude <= volume.maxLat + 0.3 &&
           longitude >= volume.minLon - 0.5 && longitude <= volume.maxLon + 0.5;
}

void AirspaceOverlay::update(double latitude, double longitude, float altitudeM, const glm::mat4 &proj,
                             const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up, int screenW,
                             int screenH)
{
    if (mCatalog.empty())
    {
        return;
    }
    if (mReady)
    {
        const double moved = GeoCoordUtils::calculateDistance(mBuiltLat, mBuiltLon, latitude, longitude);
        if (moved < kRebuildMeters)
        {
            updateInside(latitude, longitude, altitudeM, proj, eye, forward, up, screenW, screenH);
            return;
        }
    }
    rebuild(latitude, longitude);
    updateInside(latitude, longitude, altitudeM, proj, eye, forward, up, screenW, screenH);
}

void AirspaceOverlay::rebuild(double latitude, double longitude)
{
    mNearby.clear();
    for (const auto &volume : mCatalog)
    {
        if (isNearby(volume, latitude, longitude))
        {
            mNearby.push_back(volume);
        }
    }
    const auto xyz = GeoCoordUtils::convertLatLonToXYZ(latitude, longitude, 0.0);
    mCenter = glm::dvec3(xyz.x, xyz.y, xyz.z);
    mBuiltLat = latitude;
    mBuiltLon = longitude;
    mShader.clearGeometry();
    mLabels.clearGeometry();
    if (mNearby.empty())
    {
        mReady = false;
        std::cout << "Airspace overlay nearby 0 volumes" << std::endl;
        return;
    }

    std::unordered_map<uint32_t, Triangles> byColor;
    for (const auto &volume : mNearby)
    {
        const uint32_t rgba = colorForType(volume.type);
        Triangles &mesh = byColor[rgba];
        if (mesh.material.empty())
        {
            mesh.material = std::to_string(rgba);
            mShader.setColor(mesh.material, rgba);
        }
        appendWalls(mesh, volume, mCenter);
    }
    std::vector<Triangles> mesh;
    mesh.reserve(byColor.size());
    for (auto &entry : byColor)
    {
        if (!entry.second.indices.empty())
        {
            mesh.push_back(std::move(entry.second));
        }
    }
    mShader.setTriangles(mesh);
    mReady = true;
    refreshLabels();
    std::cout << "Airspace overlay nearby " << mNearby.size() << " volumes" << std::endl;
}

void AirspaceOverlay::refreshLabels()
{
    mLabels.clearGeometry();
    if (!mReady)
    {
        return;
    }
    std::vector<Triangles> labels;
    labels.reserve(mNearby.size());
    for (const auto &volume : mNearby)
    {
        appendLabels(labels, volume);
    }
    mLabels.setTriangles(labels);
}

void AirspaceOverlay::refreshLabelTextures()
{
    if (!mReady)
    {
        return;
    }
    const SDL_Color color{0x12, 0x12, 0x12, 0xFF};
    for (const auto &volume : mNearby)
    {
        if (volume.name.empty())
        {
            continue;
        }
        ensureLabelTexture(labelMaterial(volume.name), captionFor(volume), color);
    }
}

std::string AirspaceOverlay::captionFor(const Volume &volume) const
{
    std::string text = volume.name + "\n" + volume.lowerLabel + "-" + volume.upperLabel;
    const auto it = mLabelState.find(volume.name);
    if (it == mLabelState.end() || it->second.distTenths < 0)
    {
        return text;
    }
    const char *dir = "";
    switch (it->second.kind)
    {
    case Transit::Inbound:
        dir = "INBOUND";
        break;
    case Transit::Outbound:
        dir = "OUTBOUND";
        break;
    case Transit::Below:
        dir = "BELOW";
        break;
    case Transit::Above:
        dir = "ABOVE";
        break;
    case Transit::None:
        break;
    }
    char dist[32];
    std::snprintf(dist, sizeof(dist), "%.1f NM", static_cast<double>(it->second.distTenths) / 10.0);
    text += "\n";
    if (dir[0] != '\0')
    {
        text += dir;
        text += " ";
    }
    text += dist;
    return text;
}

void AirspaceOverlay::appendWalls(Triangles &out, const Volume &volume, const glm::dvec3 &center) const
{
    const auto ring = decimateRing(volume.ring);
    if (ring.size() < 3)
    {
        return;
    }
    const size_t n = ring.size();
    const unsigned int wallBase = static_cast<unsigned int>(out.vertex.size());
    out.vertex.reserve(out.vertex.size() + n * 2);
    for (size_t i = 0; i < n; ++i)
    {
        out.vertex.push_back(makeVertex(center, ring[i].lat, ring[i].lon, volume.lowerM));
        out.vertex.push_back(makeVertex(center, ring[i].lat, ring[i].lon, volume.upperM));
    }
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned int i0 = wallBase + static_cast<unsigned int>(i * 2);
        const unsigned int i1 = i0 + 1;
        const unsigned int j0 = wallBase + static_cast<unsigned int>(((i + 1) % n) * 2);
        const unsigned int j1 = j0 + 1;
        out.indices.insert(out.indices.end(), {i0, j0, j1, i0, j1, i1});
    }
}

bool AirspaceOverlay::ensureLabelTexture(const std::string &material, const std::string &label, SDL_Color color)
{
    auto drawn = mDrawnCaption.find(material);
    if (drawn != mDrawnCaption.end() && drawn->second == label)
    {
        return true;
    }
    if (TTF_Init() != 0)
    {
        std::cerr << "Airspace overlay: TTF_Init failed" << std::endl;
        return false;
    }
    const std::string fontPath = AssetPath::resolve(kFontRel);
    TTF_Font *font = TTF_OpenFont(fontPath.c_str(), 72);
    if (!font)
    {
        std::cerr << "Airspace overlay: font missing " << fontPath << std::endl;
        return false;
    }
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    SDL_Surface *surface = TTF_RenderUTF8_Blended_Wrapped(font, label.c_str(), color, 1400);
    TTF_CloseFont(font);
    if (!surface)
    {
        std::cerr << "Airspace overlay: text render failed for " << label << std::endl;
        return false;
    }
    mLabels.setTexture(material, surface);
    mDrawnCaption[material] = label;
    return true;
}

void AirspaceOverlay::addLabelQuad(Triangles &out, double lat, double lon, float altM, float northHat, float eastHat,
                                  float widthM, float heightM, bool flipU, bool frontOutward)
{
    const unsigned int base = static_cast<unsigned int>(out.vertex.size());
    for (int vi = 0; vi < 2; ++vi)
    {
        for (int ui = 0; ui < 2; ++ui)
        {
            const float u = flipU ? 1.0f - static_cast<float>(ui) : static_cast<float>(ui);
            const float v = static_cast<float>(vi);
            const float along = (static_cast<float>(ui) - 0.5f) * widthM;
            const float vert = (0.5f - v) * heightM;
            const auto ll = GeoCoordUtils::offsetMeters(lat, lon, northHat * along, eastHat * along);
            out.vertex.push_back(makeVertex(mCenter, ll.latitude, ll.longitude, altM + vert, u, v));
        }
    }
    if (frontOutward)
    {
        out.indices.insert(out.indices.end(), {base, base + 2, base + 1, base + 1, base + 2, base + 3});
    }
    else
    {
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
    }
}

void AirspaceOverlay::appendLabels(std::vector<Triangles> &mesh, const Volume &volume)
{
    const std::string caption = captionFor(volume);
    const std::string material = labelMaterial(volume.name);
    if (volume.name.empty() || !ensureLabelTexture(material, caption, SDL_Color{0x12, 0x12, 0x12, 0xFF}))
    {
        return;
    }
    const int tw = mLabels.getTextureWidth(material);
    const int th = mLabels.getTextureHeight(material);
    const float aspect = (th > 0) ? static_cast<float>(tw) / static_cast<float>(th) : 3.0f;
    if (volume.ring.size() < 2)
    {
        return;
    }

    LabelState &state = mLabelState[volume.name];
    if (!state.sInit)
    {
        state.s = nearestSOnRing(mBuiltLat, mBuiltLon, volume.ring);
        state.sInit = true;
    }
    const RingSample sample = sampleRingAtS(volume.ring, state.s);
    state.lastDrawnS = state.s;

    float north = sample.northHat;
    float east = sample.eastHat;
    float outN = east;
    float outE = -north;
    const double metersPerDegLat = 111320.0;
    const double metersPerDegLon = std::max(1000.0, metersPerDegLat * std::cos(glm::radians(sample.lat)));
    const float toCentN = static_cast<float>((volume.centLat - sample.lat) * metersPerDegLat);
    const float toCentE = static_cast<float>((volume.centLon - sample.lon) * metersPerDegLon);
    if (outN * toCentN + outE * toCentE > 0.0f)
    {
        outN = -outN;
        outE = -outE;
        north = -north;
        east = -east;
    }

    const float wallH = std::max(20.0f, volume.upperM - volume.lowerM);
    const float midAlt = 0.5f * (volume.lowerM + volume.upperM);
    const float textH = wallH * 0.96f;
    const float textW = textH * aspect;

    auto it = std::find_if(mesh.begin(), mesh.end(), [&](const Triangles &tri) { return tri.material == material; });
    if (it == mesh.end())
    {
        mesh.push_back({});
        mesh.back().material = material;
        it = mesh.end() - 1;
    }

    constexpr float kOutM = 6.0f;
    const auto outer = GeoCoordUtils::offsetMeters(sample.lat, sample.lon, outN * kOutM, outE * kOutM);
    const auto inner = GeoCoordUtils::offsetMeters(sample.lat, sample.lon, -outN * kOutM, -outE * kOutM);
    addLabelQuad(*it, outer.latitude, outer.longitude, midAlt, north, east, textW, textH, false, true);
    addLabelQuad(*it, inner.latitude, inner.longitude, midAlt, north, east, textW, textH, true, false);
}

void AirspaceOverlay::updateInside(double latitude, double longitude, float altitudeM, const glm::mat4 &proj,
                                   const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up, int screenW,
                                   int screenH)
{
    std::unordered_set<std::string> now;
    now.reserve(mNearby.size());
    bool geomDirty = false;
    bool texDirty = false;
    const uint32_t nowMs = SDL_GetTicks();
    double dt = 0.016;
    if (mLastFollowMs != 0 && nowMs > mLastFollowMs)
    {
        dt = std::clamp(static_cast<double>(nowMs - mLastFollowMs) / 1000.0, 0.001, 0.1);
    }
    mLastFollowMs = nowMs;
    const double alpha = 1.0 - std::exp(-dt / kFollowTauSec);
    const double maxStep = kFollowMaxMps * dt;

    for (const auto &volume : mNearby)
    {
        const bool onHeight = altitudeM + 30.0f >= volume.lowerM && altitudeM - 30.0f <= volume.upperM;
        const bool horizontal = pointInPolygon(latitude, longitude, volume.ring);
        const bool inVolume = onHeight && horizontal;
        if (inVolume)
        {
            now.insert(volume.name);
        }
        const double distM = distToRingM(latitude, longitude, volume.ring);
        const int tenths = std::max(0, static_cast<int>(std::lround(distM / (kMetersPerNm * 0.1))));
        LabelState &state = mLabelState[volume.name];
        const bool closing = state.lastDistM >= 0.0 && distM < state.lastDistM - 8.0;
        const bool opening = state.lastDistM >= 0.0 && distM > state.lastDistM + 8.0;
        Transit kind;
        if (altitudeM + 30.0f < volume.lowerM)
        {
            kind = Transit::Below;
        }
        else if (altitudeM - 30.0f > volume.upperM)
        {
            kind = Transit::Above;
        }
        else
        {
            kind = state.kind == Transit::Inbound || state.kind == Transit::Outbound ? state.kind : Transit::Inbound;
            if (closing)
            {
                kind = inVolume ? Transit::Outbound : Transit::Inbound;
            }
            else if (opening)
            {
                kind = inVolume ? Transit::Inbound : Transit::Outbound;
            }
        }

        const double perimeter = ringPerimeterM(volume.ring);
        if (perimeter > 1.0)
        {
            const double targetNearest = nearestSOnRing(latitude, longitude, volume.ring);
            const double targetS = sOutsideAhrs(volume, targetNearest, mCenter, proj, eye, forward, up, screenW, screenH);
            if (!state.sInit)
            {
                state.s = targetS;
                state.sInit = true;
            }
            else
            {
                double step = wrapArcDelta(targetS - state.s, perimeter) * alpha;
                step = std::clamp(step, -maxStep, maxStep);
                state.s += step;
                if (state.s < 0.0)
                {
                    state.s += perimeter;
                }
                else if (state.s >= perimeter)
                {
                    state.s -= perimeter;
                }
            }
            if (std::abs(wrapArcDelta(state.s - state.lastDrawnS, perimeter)) >= kRedrawAlongM)
            {
                geomDirty = true;
            }
        }

        if (state.kind != kind)
        {
            texDirty = true;
            geomDirty = true;
        }
        else if (state.distTenths != tenths)
        {
            texDirty = true;
        }
        state.kind = kind;
        state.distTenths = tenths;
        state.lastDistM = distM;
        if (!volume.name.empty())
        {
            int vertical = 0;
            if (altitudeM + 30.0f < volume.lowerM)
            {
                vertical = -1;
            }
            else if (altitudeM - 30.0f > volume.upperM)
            {
                vertical = 1;
            }
            NavVoice::instance().noteAirspace(volume.name, volume.type, volume.lowerLabel, distM, horizontal, vertical);
        }
    }

    for (const auto &name : now)
    {
        if (!mInside.count(name))
        {
            std::cout << "inbound " << name << std::endl;
        }
    }
    for (const auto &name : mInside)
    {
        if (!now.count(name))
        {
            std::cout << "outbound " << name << std::endl;
        }
    }

    mInside = std::move(now);
    if (geomDirty && mReady)
    {
        refreshLabels();
    }
    else if (texDirty && mReady)
    {
        refreshLabelTextures();
    }
}

void AirspaceOverlay::render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward,
                             const glm::vec3 &up)
{
    if (!mReady)
    {
        return;
    }
    const glm::vec3 eyeLocal(eye - mCenter);
    const glm::mat4 view = glm::lookAt(eyeLocal, eyeLocal + forward, up);
    const glm::mat4 mvp = proj * view;
    mShader.setMvpMatrix(mvp);
    mLabels.setMvpMatrix(mvp);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    if (mDrawWalls)
    {
        glDisable(GL_CULL_FACE);
        mShader.render();
    }
    if (mDrawLabels)
    {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        mLabels.render();
        glDisable(GL_CULL_FACE);
    }
    glDepthMask(GL_TRUE);
}
