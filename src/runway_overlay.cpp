#include "runway_overlay.h"
#include "geo_coord_utils.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <GLES3/gl3.h>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr double kNearbyMeters = 80000.0;
constexpr double kRebuildMeters = 15000.0;
constexpr char kCsvPath[] = "../resources/airports/airports.csv";

VertexTexture makeVertex(const glm::dvec3 &center, double lat, double lon, float alt, float u, float v)
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

std::vector<std::string> splitCsv(const std::string &line)
{
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ','))
    {
        fields.push_back(field);
    }
    return fields;
}

int runwayNumber(const std::string &designator)
{
    int number = 0;
    bool any = false;
    for (char c : designator)
    {
        if (c >= '0' && c <= '9')
        {
            number = number * 10 + (c - '0');
            any = true;
        }
        else if (any)
        {
            break;
        }
    }
    return any ? number : 0;
}

int reciprocalNumber(int number)
{
    if (number <= 0)
    {
        return 0;
    }
    return (number <= 18) ? number + 18 : number - 18;
}

std::string airportKey(const RunwayOverlay::Strip &strip)
{
    if (!strip.ident.empty())
    {
        return strip.ident;
    }
    return strip.name + "@" + std::to_string(static_cast<int>(strip.latitude * 10000.0)) + "," +
           std::to_string(static_cast<int>(strip.longitude * 10000.0));
}

bool parseDouble(const std::string &text, double &value)
{
    if (text.empty())
    {
        return false;
    }
    try
    {
        value = std::stod(text);
        return std::isfinite(value);
    }
    catch (...)
    {
        return false;
    }
}

bool parseFloat(const std::string &text, float &value)
{
    double parsed = 0.0;
    if (!parseDouble(text, parsed))
    {
        return false;
    }
    value = static_cast<float>(parsed);
    return true;
}
}

RunwayOverlay::RunwayOverlay()
{
    loadCatalog();
}

void RunwayOverlay::loadCatalog()
{
    std::ifstream file(kCsvPath);
    if (!file)
    {
        std::cerr << "Runway overlay: missing " << kCsvPath << std::endl;
        return;
    }

    std::string line;
    std::getline(file, line);
    std::vector<Strip> raw;
    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }
        const auto fields = splitCsv(line);
        if (fields.size() < 11)
        {
            continue;
        }
        if (fields[3] == "closed")
        {
            continue;
        }
        Strip strip;
        strip.ident = fields[0];
        strip.name = fields[1];
        strip.runway = fields[7];
        if (!parseDouble(fields[4], strip.latitude) || !parseDouble(fields[5], strip.longitude))
        {
            continue;
        }
        parseFloat(fields[6], strip.elevationM);
        if (!parseFloat(fields[8], strip.headingDeg) || !parseFloat(fields[9], strip.lengthM))
        {
            continue;
        }
        if (strip.lengthM < 50.0f)
        {
            continue;
        }
        if (!parseFloat(fields[10], strip.widthM) || strip.widthM < 5.0f)
        {
            strip.widthM = 30.0f;
        }
        raw.push_back(std::move(strip));
    }

    std::unordered_map<std::string, std::vector<int>> byAirport;
    byAirport.reserve(raw.size());
    for (int i = 0; i < static_cast<int>(raw.size()); ++i)
    {
        byAirport[airportKey(raw[static_cast<size_t>(i)])].push_back(i);
    }

    mCatalog.clear();
    mCatalog.reserve(raw.size() / 2 + 8);
    for (int i = 0; i < static_cast<int>(raw.size()); ++i)
    {
        const Strip &strip = raw[static_cast<size_t>(i)];
        const int number = runwayNumber(strip.runway);
        bool keep = (number == 0 || number <= 18);
        if (!keep)
        {
            const int recip = reciprocalNumber(number);
            const auto &group = byAirport[airportKey(strip)];
            keep = std::none_of(group.begin(), group.end(), [&](int other) {
                return runwayNumber(raw[static_cast<size_t>(other)].runway) == recip;
            });
        }
        if (keep)
        {
            mCatalog.push_back(strip);
        }
    }
    std::cout << "Runway overlay loaded " << mCatalog.size() << " strips from " << kCsvPath << std::endl;
}

void RunwayOverlay::update(double latitude, double longitude)
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
            return;
        }
    }
    rebuild(latitude, longitude);
}

void RunwayOverlay::rebuild(double latitude, double longitude)
{
    mStrips.clear();
    for (const auto &strip : mCatalog)
    {
        if (GeoCoordUtils::calculateDistance(latitude, longitude, strip.latitude, strip.longitude) <= kNearbyMeters)
        {
            mStrips.push_back(strip);
        }
    }

    const auto xyz = GeoCoordUtils::convertLatLonToXYZ(latitude, longitude, 0.0);
    mCenter = glm::dvec3(xyz.x, xyz.y, xyz.z);
    mBuiltLat = latitude;
    mBuiltLon = longitude;

    mShader.clearGeometry();
    if (mStrips.empty())
    {
        mReady = false;
        std::cout << "Runway overlay nearby 0 strips" << std::endl;
        return;
    }

    std::vector<Triangles> mesh;
    mesh.emplace_back();
    mesh.emplace_back();
    constexpr uint32_t kPavement = 0xD0D0D0FFu;
    constexpr uint32_t kCenterline = 0xF2E84CFFu;
    mesh[0].material = std::to_string(kPavement);
    mesh[1].material = std::to_string(kCenterline);
    mShader.setColor(mesh[0].material, kPavement);
    mShader.setColor(mesh[1].material, kCenterline);
    for (const auto &strip : mStrips)
    {
        addRectangle(mesh[0], strip.latitude, strip.longitude, strip.headingDeg,
                     strip.lengthM, strip.widthM, strip.elevationM, 1.5f);
        addRectangle(mesh[1], strip.latitude, strip.longitude, strip.headingDeg,
                     strip.lengthM * 0.92f, 1.8f, strip.elevationM, 1.7f);
    }
    mShader.setTriangles(mesh);
    mReady = true;
    std::cout << "Runway overlay nearby " << mStrips.size() << " strips" << std::endl;
}

void RunwayOverlay::addRectangle(Triangles &out, double lat, double lon, float headingDeg, float lengthM, float widthM,
                                float elevationM, float heightBiasM)
{
    const float heading = headingDeg * kPi / 180.0f;
    const float halfL = lengthM * 0.5f;
    const float halfW = widthM * 0.5f;
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    const float alt = elevationM + heightBiasM;

    const float alongN[2] = {c * halfL, -c * halfL};
    const float alongE[2] = {s * halfL, -s * halfL};
    const float acrossN[2] = {-s * halfW, s * halfW};
    const float acrossE[2] = {c * halfW, -c * halfW};

    const unsigned int base = static_cast<unsigned int>(out.vertex.size());
    for (int a = 0; a < 2; ++a)
    {
        for (int x = 0; x < 2; ++x)
        {
            const auto ll = GeoCoordUtils::offsetMeters(lat, lon, alongN[a] + acrossN[x], alongE[a] + acrossE[x]);
            out.vertex.push_back(makeVertex(mCenter, ll.latitude, ll.longitude, alt,
                                            static_cast<float>(a), static_cast<float>(x)));
        }
    }
    out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
}

void RunwayOverlay::render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up)
{
    if (!mReady)
    {
        return;
    }
    const glm::vec3 eyeLocal(eye - mCenter);
    const glm::mat4 view = glm::lookAt(eyeLocal, eyeLocal + forward, up);
    mShader.setMvpMatrix(proj * view);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.5f, -1.5f);
    mShader.render();
    glDisable(GL_POLYGON_OFFSET_FILL);
}
