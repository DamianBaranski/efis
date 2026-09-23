/// \file obstacle_overlay.cpp
/// Builds obstacle masts from the Czech and Polish OpenAIP GeoJSON.
#include "obstacle_overlay.h"
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
#include <regex>

namespace
{
constexpr double kNearbyMeters = 80000.0;
constexpr double kLabelMeters = 25000.0;
constexpr double kRebuildMeters = 15000.0;
constexpr int kMaxLabels = 14;
constexpr float kGroundBiasM = 2.5f;
constexpr float kUnknownWindM = 70.0f;
constexpr float kUnknownOtherM = 40.0f;
constexpr char kObsRel[] = "resources/obstacles";
constexpr char kFontRel[] = "resources/fonts/B612Mono-Regular.ttf";
constexpr uint32_t kWind = 0xF4F4F4FF;
constexpr uint32_t kChimney = 0xE25A28FF;
constexpr uint32_t kTower = 0xF0C030FF;
constexpr uint32_t kBuilding = 0x8AA0C0FF;
constexpr uint32_t kOther = 0xC8C8C8FF;

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

float quantityToM(const nlohmann::json &qty)
{
    if (!qty.is_object())
    {
        return 0.0f;
    }
    const float value = jsonFloat(qty, "value", 0.0f);
    if (value <= 0.0f)
    {
        return 0.0f;
    }
    return jsonInt(qty, "unit", 0) == 0 ? value : value * 0.3048f;
}

std::string tagValue(const nlohmann::json &tags, const char *name)
{
    if (!tags.is_object())
    {
        return {};
    }
    if (tags.value("key", std::string()) == name && tags.contains("value") && tags["value"].is_string())
    {
        return tags["value"].get<std::string>();
    }
    if (tags.contains(name) && tags[name].is_string())
    {
        return tags[name].get<std::string>();
    }
    return {};
}

float heightFromDescription(const std::string &text)
{
    if (text.empty())
    {
        return 0.0f;
    }
    std::smatch tower;
    std::smatch wing;
    static const std::regex towerRe(R"(tower height\s*(\d+(?:\.\d+)?)\s*m)", std::regex::icase);
    static const std::regex wingRe(R"((?:wing|rotor) diameter\s*(\d+(?:\.\d+)?)\s*m)", std::regex::icase);
    const bool haveTower = std::regex_search(text, tower, towerRe);
    const bool haveWing = std::regex_search(text, wing, wingRe);
    if (!haveTower)
    {
        return 0.0f;
    }
    const float hub = std::strtof(tower[1].str().c_str(), nullptr);
    if (!haveWing)
    {
        return hub;
    }
    return hub + 0.5f * std::strtof(wing[1].str().c_str(), nullptr);
}

const char *kindWord(ObstacleOverlay::Kind kind)
{
    switch (kind)
    {
    case ObstacleOverlay::Kind::Wind:
        return "WIND";
    case ObstacleOverlay::Kind::Chimney:
        return "CHIMNEY";
    case ObstacleOverlay::Kind::Tower:
        return "TOWER";
    case ObstacleOverlay::Kind::Building:
        return "BLDG";
    default:
        return "OBST";
    }
}

uint32_t kindColor(ObstacleOverlay::Kind kind)
{
    switch (kind)
    {
    case ObstacleOverlay::Kind::Wind:
        return kWind;
    case ObstacleOverlay::Kind::Chimney:
        return kChimney;
    case ObstacleOverlay::Kind::Tower:
        return kTower;
    case ObstacleOverlay::Kind::Building:
        return kBuilding;
    default:
        return kOther;
    }
}

std::string kindMaterial(ObstacleOverlay::Kind kind)
{
    return std::to_string(kindColor(kind));
}

float mastWidth(ObstacleOverlay::Kind kind)
{
    switch (kind)
    {
    case ObstacleOverlay::Kind::Chimney:
        return 8.0f;
    case ObstacleOverlay::Kind::Building:
        return 10.0f;
    case ObstacleOverlay::Kind::Wind:
        return 3.2f;
    default:
        return 4.0f;
    }
}

std::string makeLabel(const std::string &name, ObstacleOverlay::Kind kind, float heightM)
{
    std::string text = name;
    if (text.empty() || text == "Obstacle")
    {
        text = kindWord(kind);
    }
    if (text.size() > 22)
    {
        text.resize(22);
    }
    if (heightM > 1.0f)
    {
        text += " ";
        text += std::to_string(static_cast<int>(std::lround(heightM)));
    }
    return text;
}
} // namespace

ObstacleOverlay::ObstacleOverlay()
{
    loadCatalog();
}

void ObstacleOverlay::loadCatalog()
{
    namespace fs = std::filesystem;
    const fs::path dir(AssetPath::resolve(kObsRel));
    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        std::cerr << "Obstacle overlay: missing " << dir << std::endl;
        return;
    }
    for (const auto &entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() == ".geojson")
        {
            loadGeoJson(entry.path().string());
        }
    }
    std::cout << "Obstacle overlay loaded " << mCatalog.size() << " points from " << dir << std::endl;
    std::cout << "OpenAIP obstacles: https://www.openaip.net (CC BY-NC 4.0)" << std::endl;
}

void ObstacleOverlay::loadGeoJson(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
    {
        std::cerr << "Obstacle overlay: cannot read " << path << std::endl;
        return;
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
    {
        std::cerr << "Obstacle overlay: bad JSON " << path << std::endl;
        return;
    }
    const auto &features = doc.value("features", nlohmann::json::array());
    if (!features.is_array())
    {
        return;
    }
    for (const auto &feature : features)
    {
        if (!feature.is_object())
        {
            continue;
        }
        const auto &geom = feature.value("geometry", nlohmann::json::object());
        if (geom.value("type", std::string()) != "Point")
        {
            continue;
        }
        const auto &coords = geom.value("coordinates", nlohmann::json::array());
        if (!coords.is_array() || coords.size() < 2 || !coords[0].is_number() || !coords[1].is_number())
        {
            continue;
        }
        const auto &props = feature.value("properties", nlohmann::json::object());
        const auto &tags = props.value("osmTags", nlohmann::json::object());
        const std::string manMade = tagValue(tags, "man_made");
        const std::string source = tagValue(tags, "generator:source");
        const std::string method = tagValue(tags, "generator:method");
        const int type = jsonInt(props, "type", -1);

        Point point;
        point.longitude = coords[0].get<double>();
        point.latitude = coords[1].get<double>();
        point.elevationM = quantityToM(props.value("elevation", nlohmann::json::object()));
        point.heightM = quantityToM(props.value("height", nlohmann::json::object()));
        if (point.heightM <= 1.0f)
        {
            point.heightM = heightFromDescription(tagValue(tags, "description"));
        }
        if (manMade == "chimney" || type == 1)
        {
            point.kind = Kind::Chimney;
        }
        else if (manMade == "tower" || manMade == "mast" || manMade == "communications_tower" ||
                 manMade == "cooling_tower" || manMade == "antenna" || manMade == "crane" || type == 4)
        {
            point.kind = Kind::Tower;
        }
        else if (source == "wind" || method == "wind_turbine" || type == 0 || type == 3)
        {
            point.kind = Kind::Wind;
        }
        else if (type == 2)
        {
            point.kind = Kind::Building;
        }
        point.name = props.value("name", std::string());
        if (point.name == "Obstacle")
        {
            point.name.clear();
        }
        point.label = makeLabel(point.name, point.kind, point.heightM);
        mCatalog.push_back(std::move(point));
    }
}

void ObstacleOverlay::update(double latitude, double longitude)
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

void ObstacleOverlay::rebuild(double latitude, double longitude)
{
    struct Ranked
    {
        double dist = 0.0;
        Point point;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(64);
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
        Cue cue;
        cue.name = row.point.name;
        cue.latitude = row.point.latitude;
        cue.longitude = row.point.longitude;
        cue.heightM = row.point.heightM;
        cue.kind = row.point.kind;
        mNearby.push_back(std::move(cue));
    }

    const auto xyz = GeoCoordUtils::convertLatLonToXYZ(latitude, longitude, 0.0);
    mCenter = glm::dvec3(xyz.x, xyz.y, xyz.z);
    mBuiltLat = latitude;
    mBuiltLon = longitude;

    mMarks.clearGeometry();
    mLabels.clearGeometry();
    if (ranked.empty())
    {
        mReady = false;
        std::cout << "Obstacle overlay nearby 0 points" << std::endl;
        return;
    }

    std::vector<Triangles> marks;
    auto meshFor = [&](Kind kind) -> Triangles & {
        const std::string material = kindMaterial(kind);
        for (auto &mesh : marks)
        {
            if (mesh.material == material)
            {
                return mesh;
            }
        }
        marks.push_back({});
        marks.back().material = material;
        mMarks.setColor(material, kindColor(kind));
        return marks.back();
    };

    std::vector<Triangles> labels;
    int labeled = 0;
    for (const auto &row : ranked)
    {
        appendMast(meshFor(row.point.kind), row.point);
        if (labeled < kMaxLabels && row.dist <= kLabelMeters)
        {
            appendLabel(labels, row.point);
            ++labeled;
        }
    }
    mMarks.setTriangles(marks);
    mLabels.setTriangles(labels);
    mReady = true;
    std::cout << "Obstacle overlay nearby " << ranked.size() << " points, " << labeled << " labels" << std::endl;
}

void ObstacleOverlay::appendMast(Triangles &out, const Point &point) const
{
    const float shown = point.heightM > 1.0f ? point.heightM
                                              : (point.kind == Kind::Wind ? kUnknownWindM : kUnknownOtherM);
    const float alt = point.elevationM + kGroundBiasM;
    addGroundFan(out, point.latitude, point.longitude, point.elevationM, 14.0f, 4);
    const float mid = alt + shown * 0.5f;
    const float width = mastWidth(point.kind);
    addVerticalQuad(out, point.latitude, point.longitude, mid, 1.0f, 0.0f, width, shown, false);
    addVerticalQuad(out, point.latitude, point.longitude, mid, 0.0f, 1.0f, width, shown, false);
    if (point.kind == Kind::Wind)
    {
        const float span = std::clamp(shown * 0.55f, 18.0f, 90.0f);
        const float top = alt + shown;
        addVerticalQuad(out, point.latitude, point.longitude, top, 1.0f, 0.0f, span, 2.4f, false);
        addVerticalQuad(out, point.latitude, point.longitude, top, 0.0f, 1.0f, span, 2.4f, false);
    }
}

void ObstacleOverlay::appendLabel(std::vector<Triangles> &mesh, const Point &point)
{
    if (point.label.empty() || !ensureLabelTexture(point.label))
    {
        return;
    }
    const std::string material = std::string("obst:") + point.label;
    const int tw = mLabels.getTextureWidth(material);
    const int th = mLabels.getTextureHeight(material);
    const float aspect = (th > 0) ? static_cast<float>(tw) / static_cast<float>(th) : 4.0f;
    const float textH = 28.0f;
    const float textW = textH * aspect;
    const float shown = point.heightM > 1.0f ? point.heightM
                                              : (point.kind == Kind::Wind ? kUnknownWindM : kUnknownOtherM);
    const float mid = point.elevationM + kGroundBiasM + shown + textH * 0.6f;

    auto it = std::find_if(mesh.begin(), mesh.end(), [&](const Triangles &tri) { return tri.material == material; });
    if (it == mesh.end())
    {
        mesh.push_back({});
        mesh.back().material = material;
        it = mesh.end() - 1;
    }
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 0.0f, 1.0f, textW, textH, false);
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 0.0f, 1.0f, textW, textH, true);
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 1.0f, 0.0f, textW, textH, false);
    addVerticalQuad(*it, point.latitude, point.longitude, mid, 1.0f, 0.0f, textW, textH, true);
}

void ObstacleOverlay::addGroundFan(Triangles &out, double lat, double lon, float elevM, float radiusM, int sides) const
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
        const float ang = static_cast<float>(i) * 2.0f * kPi / static_cast<float>(sides);
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

void ObstacleOverlay::addVerticalQuad(Triangles &out, double lat, double lon, float altMid, float northHat,
                                      float eastHat, float widthM, float heightM, bool flipU) const
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

bool ObstacleOverlay::ensureLabelTexture(const std::string &label)
{
    const std::string material = std::string("obst:") + label;
    if (mReadyLabels.count(material) != 0)
    {
        return true;
    }
    if (TTF_Init() != 0)
    {
        std::cerr << "Obstacle overlay: TTF_Init failed" << std::endl;
        return false;
    }
    const std::string fontPath = AssetPath::resolve(kFontRel);
    TTF_Font *font = TTF_OpenFont(fontPath.c_str(), 72);
    if (!font)
    {
        std::cerr << "Obstacle overlay: font missing " << fontPath << std::endl;
        return false;
    }
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    const SDL_Color fg{0x12, 0x12, 0x12, 0xFF};
    const SDL_Color bg{0xF7, 0xF1, 0xE4, 0xFF};
    SDL_Surface *surface = TTF_RenderUTF8_Shaded(font, label.c_str(), fg, bg);
    TTF_CloseFont(font);
    if (!surface)
    {
        std::cerr << "Obstacle overlay: text render failed for " << label << std::endl;
        return false;
    }
    mLabels.setTexture(material, surface);
    mReadyLabels.insert(material);
    return true;
}

void ObstacleOverlay::render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up)
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
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    mMarks.render();
    mLabels.render();
    glDepthMask(GL_TRUE);
}
