#include "runway_overlay.h"
#include "geo_coord_utils.h"
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <GLES3/gl3.h>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr double kNearbyMeters = 80000.0;
constexpr double kRebuildMeters = 15000.0;
constexpr char kCsvPath[] = "../resources/airports/airports.csv";
constexpr char kFontPath[] = "../resources/fonts/B612Mono-Regular.ttf";
constexpr uint32_t kPavement = 0x1A1A1CFF;
constexpr uint32_t kMarking = 0xC8C8C8FF;

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

char runwaySide(const std::string &designator)
{
    for (char c : designator)
    {
        if (c == 'L' || c == 'R' || c == 'C')
        {
            return c;
        }
        if (c == 'l' || c == 'r' || c == 'c')
        {
            return static_cast<char>(c - 32);
        }
    }
    return 0;
}

int reciprocalNumber(int number)
{
    if (number <= 0)
    {
        return 0;
    }
    return (number <= 18) ? number + 18 : number - 18;
}

char reciprocalSide(char side)
{
    if (side == 'L')
    {
        return 'R';
    }
    if (side == 'R')
    {
        return 'L';
    }
    return side;
}

std::string formatDesignator(int number, char side)
{
    if (number <= 0)
    {
        return {};
    }
    char buf[16];
    if (side != 0)
    {
        std::snprintf(buf, sizeof(buf), "%02d%c", number, side);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%02d", number);
    }
    return buf;
}

std::string paddedDesignator(const std::string &designator)
{
    const int number = runwayNumber(designator);
    if (number <= 0)
    {
        return designator;
    }
    return formatDesignator(number, runwaySide(designator));
}

std::string reciprocalDesignator(const std::string &designator)
{
    const int number = runwayNumber(designator);
    if (number <= 0)
    {
        return designator;
    }
    return formatDesignator(reciprocalNumber(number), reciprocalSide(runwaySide(designator)));
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

std::string labelMaterial(const std::string &label)
{
    return std::string("rwy:") + label;
}


constexpr char kOsmPath[] = "../resources/airports/osm_runways.csv";

struct OsmWay
{
    std::string icao;
    std::string ref;
    double latitude = 0.0;
    double longitude = 0.0;
    float headingDeg = 0.0f;
    float lengthM = 0.0f;
    float widthM = 0.0f;
};

std::vector<std::string> splitRef(const std::string &ref)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : ref)
    {
        if (c == '/' || c == '-' || c == ' ')
        {
            if (!cur.empty())
            {
                parts.push_back(paddedDesignator(cur));
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
    {
        parts.push_back(paddedDesignator(cur));
    }
    return parts;
}

bool osmRefMatches(const std::string &osmRef, const std::string &designator)
{
    if (osmRef.empty() || designator.empty())
    {
        return false;
    }
    const std::string padded = paddedDesignator(designator);
    for (const auto &part : splitRef(osmRef))
    {
        if (part == padded)
        {
            return true;
        }
    }
    return false;
}

std::vector<OsmWay> loadOsmWays()
{
    std::vector<OsmWay> ways;
    std::ifstream file(kOsmPath);
    if (!file)
    {
        return ways;
    }
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line))
    {
        if (line.empty())
        {
            continue;
        }
        const auto fields = splitCsv(line);
        if (fields.size() < 7)
        {
            continue;
        }
        OsmWay way;
        way.icao = fields[0];
        way.ref = fields[1];
        if (!parseDouble(fields[2], way.latitude) || !parseDouble(fields[3], way.longitude))
        {
            continue;
        }
        if (!parseFloat(fields[4], way.headingDeg) || !parseFloat(fields[5], way.lengthM))
        {
            continue;
        }
        parseFloat(fields[6], way.widthM);
        if (way.widthM < 5.0f)
        {
            way.widthM = 50.0f;
        }
        ways.push_back(way);
    }
    return ways;
}

void applyOsmWays(std::vector<RunwayOverlay::Strip> &strips, std::unordered_set<int> &aligned)
{
    const auto ways = loadOsmWays();
    if (ways.empty())
    {
        return;
    }
    for (int i = 0; i < static_cast<int>(strips.size()); ++i)
    {
        RunwayOverlay::Strip &strip = strips[static_cast<size_t>(i)];
        if (strip.ident.empty())
        {
            continue;
        }
        const OsmWay *match = nullptr;
        for (const auto &way : ways)
        {
            if (way.icao != strip.ident)
            {
                continue;
            }
            if (osmRefMatches(way.ref, strip.runway) || osmRefMatches(way.ref, strip.runwayRecip))
            {
                match = &way;
                break;
            }
        }
        if (!match)
        {
            continue;
        }
        strip.latitude = match->latitude;
        strip.longitude = match->longitude;
        strip.headingDeg = match->headingDeg;
        strip.lengthM = match->lengthM;
        strip.widthM = match->widthM;
        aligned.insert(i);
        std::cout << "OSM " << strip.ident << " " << strip.runway << "/" << strip.runwayRecip
                  << " " << strip.lengthM << "x" << strip.widthM << " m hdg " << strip.headingDeg << std::endl;
    }
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
        strip.runway = paddedDesignator(fields[7]);
        strip.runwayRecip = reciprocalDesignator(strip.runway);
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

    std::vector<Strip> primaries;
    primaries.reserve(raw.size() / 2 + 8);
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
            primaries.push_back(strip);
        }
    }

    std::unordered_set<int> aligned;
    applyOsmWays(primaries, aligned);

    // Fallback: shift L/R off the shared ARP when OSM has no centerline.
    std::unordered_map<std::string, std::vector<int>> parallels;
    for (int i = 0; i < static_cast<int>(primaries.size()); ++i)
    {
        const int number = runwayNumber(primaries[static_cast<size_t>(i)].runway);
        if (number <= 0)
        {
            continue;
        }
        parallels[airportKey(primaries[static_cast<size_t>(i)]) + "|" + std::to_string(number)].push_back(i);
    }
    for (const auto &entry : parallels)
    {
        int left = -1;
        int right = -1;
        float widthL = 50.0f;
        float widthR = 50.0f;
        for (int idx : entry.second)
        {
            Strip &strip = primaries[static_cast<size_t>(idx)];
            const char side = runwaySide(strip.runway);
            if (side == 'L')
            {
                left = idx;
                widthL = strip.widthM;
            }
            else if (side == 'R')
            {
                right = idx;
                widthR = strip.widthM;
            }
        }
        if (left < 0 || right < 0 || aligned.count(left) || aligned.count(right))
        {
            continue;
        }
        const float dist = 0.25f * (widthL + widthR) + 4.0f;
        auto shift = [&](int idx, float towardRight) {
            Strip &strip = primaries[static_cast<size_t>(idx)];
            const float heading = strip.headingDeg * kPi / 180.0f;
            const float north = -std::sin(heading) * towardRight;
            const float east = std::cos(heading) * towardRight;
            const auto ll = GeoCoordUtils::offsetMeters(strip.latitude, strip.longitude, north, east);
            strip.latitude = ll.latitude;
            strip.longitude = ll.longitude;
        };
        shift(left, -dist);
        shift(right, dist);
    }

    mCatalog = std::move(primaries);
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

    std::vector<Triangles> mesh(2);
    mesh[0].material = std::to_string(kPavement);
    mesh[1].material = std::to_string(kMarking);
    mShader.setColor(mesh[0].material, kPavement);
    mShader.setColor(mesh[1].material, kMarking);
    for (const auto &strip : mStrips)
    {
        addRectangle(mesh[0], strip.latitude, strip.longitude, strip.headingDeg, 0.0f, strip.lengthM, strip.widthM,
                     strip.elevationM, 1.5f);
        addDashes(mesh[1], strip);
        addNumbers(mesh, strip);
        if (strip.ident == "EPMR" || strip.ident == "EPWS")
        {
            std::cout << "Runway " << strip.ident << " " << strip.runway << "/" << strip.runwayRecip
                      << " " << strip.lengthM << "x" << strip.widthM << " m hdg " << strip.headingDeg << std::endl;
        }
    }
    mShader.setTriangles(mesh);
    mReady = true;
    std::cout << "Runway overlay nearby " << mStrips.size() << " strips" << std::endl;
}

void RunwayOverlay::addRectangle(Triangles &out, double lat, double lon, float headingDeg, float alongM, float lengthM,
                                float widthM, float elevationM, float heightBiasM)
{
    const float heading = headingDeg * kPi / 180.0f;
    const float halfL = lengthM * 0.5f;
    const float halfW = widthM * 0.5f;
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    const float alt = elevationM + heightBiasM;
    const float midN = c * alongM;
    const float midE = s * alongM;

    const float alongN[2] = {c * halfL, -c * halfL};
    const float alongE[2] = {s * halfL, -s * halfL};
    const float acrossN[2] = {-s * halfW, s * halfW};
    const float acrossE[2] = {c * halfW, -c * halfW};

    const unsigned int base = static_cast<unsigned int>(out.vertex.size());
    for (int a = 0; a < 2; ++a)
    {
        for (int x = 0; x < 2; ++x)
        {
            const auto ll = GeoCoordUtils::offsetMeters(lat, lon, midN + alongN[a] + acrossN[x],
                                                        midE + alongE[a] + acrossE[x]);
            out.vertex.push_back(makeVertex(mCenter, ll.latitude, ll.longitude, alt,
                                            static_cast<float>(a), static_cast<float>(x)));
        }
    }
    out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
}

void RunwayOverlay::addLabelQuad(Triangles &out, double lat, double lon, float headingDeg, float alongM, float lengthM,
                                float widthM, float elevationM, float heightBiasM)
{
    const float heading = headingDeg * kPi / 180.0f;
    const float halfL = lengthM * 0.5f;
    const float halfW = widthM * 0.5f;
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    const float alt = elevationM + heightBiasM;
    const float midN = c * alongM;
    const float midE = s * alongM;

    // a=0 is the top of the glyphs (down the landing heading).
    const float alongN[2] = {c * halfL, -c * halfL};
    const float alongE[2] = {s * halfL, -s * halfL};
    const float leftN = s * halfW;
    const float leftE = -c * halfW;
    const float rightN = -s * halfW;
    const float rightE = c * halfW;
    const float acrossN[2] = {leftN, rightN};
    const float acrossE[2] = {leftE, rightE};
    const float u[2] = {0.0f, 1.0f};
    const float v[2] = {0.0f, 1.0f};

    const unsigned int base = static_cast<unsigned int>(out.vertex.size());
    for (int a = 0; a < 2; ++a)
    {
        for (int x = 0; x < 2; ++x)
        {
            const auto ll = GeoCoordUtils::offsetMeters(lat, lon, midN + alongN[a] + acrossN[x],
                                                        midE + alongE[a] + acrossE[x]);
            out.vertex.push_back(makeVertex(mCenter, ll.latitude, ll.longitude, alt, u[x], v[a]));
        }
    }
    out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
}

void RunwayOverlay::addDashes(Triangles &out, const Strip &strip)
{
    const float dash = std::clamp(strip.lengthM * 0.05f, 12.0f, 30.0f);
    const float gap = dash * 0.7f;
    const float lineW = std::clamp(strip.widthM * 0.04f, 1.2f, 2.4f);
    const float reserve = std::min(strip.lengthM * 0.16f, 60.0f);
    const float usable = strip.lengthM - 2.0f * reserve;
    if (usable < dash)
    {
        return;
    }
    const int count = std::max(1, static_cast<int>(std::floor((usable + gap) / (dash + gap))));
    const float span = static_cast<float>(count) * dash + static_cast<float>(count - 1) * gap;
    float cursor = -0.5f * span;
    for (int i = 0; i < count; ++i)
    {
        addRectangle(out, strip.latitude, strip.longitude, strip.headingDeg, cursor + dash * 0.5f, dash, lineW,
                     strip.elevationM, 1.7f);
        cursor += dash + gap;
    }
}

bool RunwayOverlay::ensureLabelTexture(const std::string &label)
{
    const std::string name = labelMaterial(label);
    static std::unordered_set<std::string> ready;
    if (ready.count(name) != 0)
    {
        return true;
    }
    if (TTF_Init() != 0)
    {
        std::cerr << "Runway overlay: TTF_Init failed" << std::endl;
        return false;
    }
    TTF_Font *font = TTF_OpenFont(kFontPath, 96);
    if (!font)
    {
        std::cerr << "Runway overlay: font missing " << kFontPath << std::endl;
        return false;
    }
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    const SDL_Color color{0xC8, 0xC8, 0xC8, 0xFF};
    SDL_Surface *surface = TTF_RenderText_Blended(font, label.c_str(), color);
    TTF_CloseFont(font);
    if (!surface)
    {
        std::cerr << "Runway overlay: text render failed for " << label << std::endl;
        return false;
    }
    mShader.setTexture(name, surface);
    ready.insert(name);
    return true;
}

void RunwayOverlay::addNumbers(std::vector<Triangles> &mesh, const Strip &strip)
{
    const float height = std::clamp(std::min(strip.widthM * 0.42f, strip.lengthM * 0.08f), 10.0f, 24.0f);
    const float inset = std::min(strip.lengthM * 0.5f - height, height * 1.35f + 12.0f);
    if (inset < height)
    {
        return;
    }

    auto addOne = [&](const std::string &label, float headingDeg, float alongM) {
        if (label.empty() || !ensureLabelTexture(label))
        {
            return;
        }
        const std::string name = labelMaterial(label);
        const int tw = mShader.getTextureWidth(name);
        const int th = mShader.getTextureHeight(name);
        const float aspect = (th > 0) ? static_cast<float>(tw) / static_cast<float>(th) : 1.6f;
        const float width = std::min(strip.widthM * 0.72f, height * aspect);
        auto it = std::find_if(mesh.begin(), mesh.end(), [&](const Triangles &tri) { return tri.material == name; });
        if (it == mesh.end())
        {
            mesh.push_back({});
            mesh.back().material = name;
            it = mesh.end() - 1;
        }
        addLabelQuad(*it, strip.latitude, strip.longitude, headingDeg, alongM, height, width, strip.elevationM, 1.9f);
    };

    const float half = strip.lengthM * 0.5f;
    addOne(strip.runway, strip.headingDeg, -half + inset);
    addOne(strip.runwayRecip, strip.headingDeg + 180.0f, half - inset);
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.5f, -1.5f);
    mShader.render();
    glDisable(GL_POLYGON_OFFSET_FILL);
}
