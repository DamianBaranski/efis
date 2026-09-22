/// \file vrp_overlay.cpp
/// Builds reporting-point marks and name plates from the OpenAIP catalog.
#include "vrp_overlay.h"
#include "asset_path.h"
#include "geo_coord_utils.h"
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <GLES3/gl3.h>
#include <iostream>
#include <nlohmann/json.hpp>

namespace
{
constexpr double kNearbyMeters = 80000.0;
constexpr double kLabelMeters = 35000.0;
constexpr double kCompulsoryLabelMeters = 50000.0;
constexpr double kRebuildMeters = 15000.0;
constexpr int kMaxLabels = 18;
constexpr float kMarkRadiusM = 55.0f;
constexpr float kOptionalRadiusM = 42.0f;
constexpr float kMastH = 60.0f;
constexpr float kMastW = 2.4f;
constexpr float kTextH = 32.0f;
constexpr float kGroundBiasM = 2.5f;
constexpr char kVrpRel[] = "resources/vrp";
constexpr char kFontRel[] = "resources/fonts/B612Mono-Regular.ttf";
constexpr uint32_t kCompulsory = 0xD01870FF;
constexpr uint32_t kOptional = 0xD01870AA;

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

std::string labelMaterial(const std::string &name)
{
    return std::string("vrp:") + name;
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

float elevationToM(const nlohmann::json &elev)
{
    if (!elev.is_object())
    {
        return 151.0f;
    }
    const float value = jsonFloat(elev, "value", 151.0f);
    return jsonInt(elev, "unit", 0) == 0 ? value : value * 0.3048f;
}
} // namespace

VrpOverlay::VrpOverlay()
{
    loadCatalog();
}

void VrpOverlay::loadCatalog()
{
    namespace fs = std::filesystem;
    const fs::path dir(AssetPath::resolve(kVrpRel));
    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        std::cerr << "VRP overlay: missing " << dir << std::endl;
        return;
    }
    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() == ".json")
        {
            loadJson(entry.path().string());
        }
    }
    std::cout << "VRP overlay loaded " << mCatalog.size() << " points from " << dir << std::endl;
}

void VrpOverlay::loadJson(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "VRP overlay: cannot read " << path << std::endl;
        return;
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_array())
    {
        std::cerr << "VRP overlay: bad JSON " << path << std::endl;
        return;
    }
    for (const auto &item : doc)
    {
        if (!item.is_object())
        {
            continue;
        }
        const auto &geom = item.value("geometry", nlohmann::json::object());
        const auto &coords = geom.value("coordinates", nlohmann::json::array());
        if (!coords.is_array() || coords.size() < 2 || !coords[0].is_number() || !coords[1].is_number())
        {
            continue;
        }
        Point point;
        point.name = item.value("name", std::string());
        if (point.name.empty())
        {
            continue;
        }
        point.longitude = coords[0].get<double>();
        point.latitude = coords[1].get<double>();
        point.elevationM = elevationToM(item.value("elevation", nlohmann::json::object()));
        point.compulsory = item.value("compulsory", false);
        mCatalog.push_back(std::move(point));
    }
}

void VrpOverlay::update(double latitude, double longitude)
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

void VrpOverlay::rebuild(double latitude, double longitude)
{
    struct Ranked
    {
        double dist = 0.0;
        Point point;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(mCatalog.size());
    for (const auto &point : mCatalog)
    {
        const double dist = GeoCoordUtils::calculateDistance(latitude, longitude, point.latitude, point.longitude);
        if (dist <= kNearbyMeters)
        {
            ranked.push_back({dist, point});
        }
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) { return a.dist < b.dist; });

    mNearby.clear();
    mNearby.reserve(ranked.size());
    for (const auto &row : ranked)
    {
        mNearby.push_back(row.point);
    }

    const auto xyz = GeoCoordUtils::convertLatLonToXYZ(latitude, longitude, 0.0);
    mCenter = glm::dvec3(xyz.x, xyz.y, xyz.z);
    mBuiltLat = latitude;
    mBuiltLon = longitude;

    mMarks.clearGeometry();
    mLabels.clearGeometry();
    if (mNearby.empty())
    {
        mReady = false;
        std::cout << "VRP overlay nearby 0 points" << std::endl;
        return;
    }

    std::vector<Triangles> marks(1);
    marks[0].material = std::to_string(kCompulsory);
    mMarks.setColor(marks[0].material, kCompulsory);
    mMarks.setColor(std::to_string(kOptional), kOptional);

    std::vector<Triangles> optional;
    optional.push_back({});
    optional.back().material = std::to_string(kOptional);

    std::vector<Triangles> labels;
    int labeled = 0;
    for (size_t i = 0; i < ranked.size(); ++i)
    {
        const auto &row = ranked[i];
        if (row.point.compulsory)
        {
            appendMark(marks[0], row.point);
            appendMast(marks[0], row.point);
        }
        else
        {
            appendMark(optional[0], row.point);
            appendMast(optional[0], row.point);
        }
        const double labelRange = row.point.compulsory ? kCompulsoryLabelMeters : kLabelMeters;
        if (labeled < kMaxLabels && row.dist <= labelRange)
        {
            appendLabel(labels, row.point);
            ++labeled;
        }
    }
    if (!optional[0].vertex.empty())
    {
        marks.push_back(std::move(optional[0]));
    }
    mMarks.setTriangles(marks);
    mLabels.setTriangles(labels);
    mReady = true;
    std::cout << "VRP overlay nearby " << mNearby.size() << " points, " << labeled << " labels" << std::endl;
}

void VrpOverlay::appendMark(Triangles &out, const Point &point) const
{
    if (point.compulsory)
    {
        addGroundFan(out, point.latitude, point.longitude, point.elevationM, kMarkRadiusM, 3);
    }
    else
    {
        addGroundFan(out, point.latitude, point.longitude, point.elevationM, kOptionalRadiusM, 8);
    }
}

void VrpOverlay::appendMast(Triangles &out, const Point &point) const
{
    const float mid = point.elevationM + kGroundBiasM + kMastH * 0.5f;
    addVerticalQuad(out, point.latitude, point.longitude, mid, 1.0f, 0.0f, kMastW, kMastH, false);
    addVerticalQuad(out, point.latitude, point.longitude, mid, 0.0f, 1.0f, kMastW, kMastH, false);
}

void VrpOverlay::appendLabel(std::vector<Triangles> &mesh, const Point &point)
{
    const std::string material = labelMaterial(point.name);
    if (!ensureLabelTexture(point.name))
    {
        return;
    }
    const int tw = mLabels.getTextureWidth(material);
    const int th = mLabels.getTextureHeight(material);
    const float aspect = (th > 0) ? static_cast<float>(tw) / static_cast<float>(th) : 3.0f;
    const float textW = kTextH * aspect;
    const float mid = point.elevationM + kGroundBiasM + kMastH + kTextH * 0.5f;

    auto it = std::find_if(mesh.begin(), mesh.end(), [&](const Triangles &tri) { return tri.material == material; });
    if (it == mesh.end())
    {
        mesh.push_back({});
        mesh.back().material = material;
        it = mesh.end() - 1;
    }
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 0.0f, 1.0f, textW, kTextH, false);
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 0.0f, 1.0f, textW, kTextH, true);
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 1.0f, 0.0f, textW, kTextH, false);
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 1.0f, 0.0f, textW, kTextH, true);
}

void VrpOverlay::addGroundFan(Triangles &out, double lat, double lon, float elevM, float radiusM, int sides) const
{
    if (sides < 3)
    {
        return;
    }
    const float alt = elevM + kGroundBiasM;
    const unsigned int center = static_cast<unsigned int>(out.vertex.size());
    out.vertex.push_back(makeVertex(mCenter, lat, lon, alt, 0.5f, 0.5f));
    constexpr float kPi = 3.14159265358979323846f;
    for (int i = 0; i < sides; ++i)
    {
        const float ang = -0.5f * kPi + static_cast<float>(i) * 2.0f * kPi / static_cast<float>(sides);
        const auto ll = GeoCoordUtils::offsetMeters(lat, lon, std::cos(ang) * radiusM, std::sin(ang) * radiusM);
        out.vertex.push_back(makeVertex(mCenter, ll.latitude, ll.longitude, alt, 0.0f, 0.0f));
    }
    for (int i = 0; i < sides; ++i)
    {
        const unsigned int a = center + 1 + static_cast<unsigned int>(i);
        const unsigned int b = center + 1 + static_cast<unsigned int>((i + 1) % sides);
        out.indices.insert(out.indices.end(), {center, a, b});
    }
}

void VrpOverlay::addVerticalQuad(Triangles &out, double lat, double lon, float altMid, float northHat, float eastHat,
                                float widthM, float heightM, bool flipU) const
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
            out.vertex.push_back(makeVertex(mCenter, ll.latitude, ll.longitude, altMid + vert, u, v));
        }
    }
    if (flipU)
    {
        out.indices.insert(out.indices.end(), {base, base + 2, base + 1, base + 1, base + 2, base + 3});
    }
    else
    {
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
    }
}

bool VrpOverlay::ensureLabelTexture(const std::string &name)
{
    const std::string material = labelMaterial(name);
    if (mReadyLabels.count(material) != 0)
    {
        return true;
    }
    if (TTF_Init() != 0)
    {
        std::cerr << "VRP overlay: TTF_Init failed" << std::endl;
        return false;
    }
    const std::string fontPath = AssetPath::resolve(kFontRel);
    TTF_Font *font = TTF_OpenFont(fontPath.c_str(), 96);
    if (!font)
    {
        std::cerr << "VRP overlay: font missing " << fontPath << std::endl;
        return false;
    }
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    const SDL_Color fg{0x12, 0x12, 0x12, 0xFF};
    const SDL_Color bg{0xF4, 0xE6, 0xEE, 0xFF};
    SDL_Surface *surface = TTF_RenderUTF8_Shaded(font, name.c_str(), fg, bg);
    TTF_CloseFont(font);
    if (!surface)
    {
        std::cerr << "VRP overlay: text render failed for " << name << std::endl;
        return false;
    }
    mLabels.setTexture(material, surface);
    mReadyLabels.insert(material);
    return true;
}

void VrpOverlay::render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up)
{
    if (!mReady)
    {
        return;
    }
    const glm::vec3 eyeLocal(eye - mCenter);
    const glm::mat4 view = glm::lookAt(eyeLocal, eyeLocal + forward, up);
    const glm::mat4 mvp = proj * view;
    mMarks.setMvpMatrix(mvp);
    mLabels.setMvpMatrix(mvp);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.5f, -1.5f);
    glDisable(GL_CULL_FACE);
    mMarks.render();
    glDisable(GL_POLYGON_OFFSET_FILL);
    mLabels.render();
}
