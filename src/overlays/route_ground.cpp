/// \file route_ground.cpp
/// Builds a ground ribbon along the route in front of the aircraft.
#include "route_ground.h"
#include "bucket_container.h"
#include "flight_plan.h"
#include "geo_coord_utils.h"
#include "nav_db.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kVFovRad = 60.0f * kPi / 180.0f;
/// Project magenta, 30% opacity. Shader colors are RGBA from the high byte.
constexpr uint32_t kMagenta30 = 0xFF2FD04Du;
constexpr float kStepM = 500.0f;
constexpr float kGroundBiasM = 12.0f;
constexpr float kDefaultGroundM = 150.0f;
constexpr double kMetersPerDeg = 111320.0;

struct Node
{
    double lat = 0.0;
    double lon = 0.0;
    float alt = 0.0f;
};

float groundAlt(const Waypoint *wpt)
{
    if (wpt != nullptr && wpt->elevationFt > 0.0f)
    {
        return wpt->elevationFt * 0.3048f;
    }
    return -1.0f;
}

glm::dvec2 toNorthEast(double fromLat, double fromLon, double lat, double lon)
{
    const double north = (lat - fromLat) * kMetersPerDeg;
    const double east = (lon - fromLon) * kMetersPerDeg * std::cos(fromLat * kPi / 180.0);
    return glm::dvec2(north, east);
}

Node lerpNode(const Node &a, const Node &b, double t)
{
    Node out;
    out.lat = a.lat + (b.lat - a.lat) * t;
    out.lon = a.lon + (b.lon - a.lon) * t;
    out.alt = a.alt + (b.alt - a.alt) * static_cast<float>(t);
    return out;
}

void appendLeg(std::vector<Node> &out, const Node &from, const Node &to)
{
    const glm::dvec2 delta = toNorthEast(from.lat, from.lon, to.lat, to.lon);
    const double dist = std::hypot(delta.x, delta.y);
    const int steps = std::max(1, static_cast<int>(std::ceil(dist / kStepM)));
    for (int step = 1; step <= steps; ++step)
    {
        out.push_back(lerpNode(from, to, static_cast<double>(step) / static_cast<double>(steps)));
    }
}

/// World width that covers one seventh of the screen where the bottom edge meets the ground.
float ribbonWidthM(const SceneFrame &frame, const glm::vec3 &geodeticUp, float groundM)
{
    const float agl = std::max(30.0f, frame.altitudeM - groundM);
    const float aspect = static_cast<float>(std::max(1, frame.screenW)) / static_cast<float>(std::max(1, frame.screenH));
    const float halfV = kVFovRad * 0.5f;
    const float halfH = std::atan(std::tan(halfV) * aspect);
    const glm::vec3 bottom = glm::normalize(frame.forward * std::cos(halfV) - frame.up * std::sin(halfV));
    const float drop = -glm::dot(bottom, geodeticUp);
    float axisDist = agl / std::tan(halfV);
    if (drop > 0.08f)
    {
        const float slant = agl / drop;
        axisDist = std::max(30.0f, slant * glm::dot(bottom, frame.forward));
    }
    const float viewWidth = 2.0f * axisDist * std::tan(halfH);
    return std::max(8.0f, viewWidth / 7.0f);
}

VertexTexture vertexAt(const glm::dvec3 &center, double lat, double lon, float alt)
{
    const auto xyz = GeoCoordUtils::convertLatLonToXYZ(lat, lon, alt);
    VertexTexture vt{};
    vt.vertex.x = static_cast<float>(xyz.x - center.x);
    vt.vertex.y = static_cast<float>(xyz.y - center.y);
    vt.vertex.z = static_cast<float>(xyz.z - center.z);
    vt.textureCoord.x = 0.5f;
    vt.textureCoord.y = 0.5f;
    return vt;
}
} // namespace

RouteGround::RouteGround() = default;

void RouteGround::update(const SceneFrame &) {}

bool RouteGround::needsRebuild(const SceneFrame &frame) const
{
    if (!mReady || mPlan == nullptr)
    {
        return true;
    }
    if (mPlan->revision() != mBuiltRevision || frame.screenW != mBuiltW || frame.screenH != mBuiltH)
    {
        return true;
    }
    if (std::fabs(frame.altitudeM - mBuiltAlt) > 25.0f)
    {
        return true;
    }
    if (glm::dot(frame.forward, mBuiltForward) < 0.996f)
    {
        return true;
    }
    const double north = (frame.latitude - mBuiltLat) * 111320.0;
    const double east =
        (frame.longitude - mBuiltLon) * 111320.0 * std::cos(mBuiltLat * 3.14159265358979323846 / 180.0);
    return north * north + east * east > 80.0 * 80.0;
}

void RouteGround::rebuild(const SceneFrame &frame)
{
    mReady = false;
    if (mPlan == nullptr || frame.screenW <= 0 || frame.screenH <= 0)
    {
        return;
    }

    std::vector<Node> route;
    route.reserve(mPlan->route().size());
    for (const std::string &ident : mPlan->route())
    {
        const Waypoint *wpt = NavDb::instance().find(ident);
        if (wpt == nullptr)
        {
            continue;
        }
        Node node;
        node.lat = wpt->lat;
        node.lon = wpt->lon;
        const float known = groundAlt(wpt);
        node.alt = known > 0.0f ? known : kDefaultGroundM;
        route.push_back(node);
    }
    if (route.size() < 2)
    {
        return;
    }

    const glm::dvec2 here(0.0);
    int bestSeg = 0;
    double bestT = 0.0;
    double bestDist = 1.0e12;
    for (size_t i = 0; i + 1 < route.size(); ++i)
    {
        const glm::dvec2 a = toNorthEast(frame.latitude, frame.longitude, route[i].lat, route[i].lon);
        const glm::dvec2 b = toNorthEast(frame.latitude, frame.longitude, route[i + 1].lat, route[i + 1].lon);
        const glm::dvec2 ab = b - a;
        const double len2 = glm::dot(ab, ab);
        double t = 0.0;
        if (len2 > 1.0)
        {
            t = std::clamp(glm::dot(here - a, ab) / len2, 0.0, 1.0);
        }
        const glm::dvec2 hit = a + ab * t;
        const double dist = glm::length(hit);
        if (dist < bestDist)
        {
            bestDist = dist;
            bestSeg = static_cast<int>(i);
            bestT = t;
        }
    }

    std::vector<Node> ahead;
    ahead.push_back(lerpNode(route[static_cast<size_t>(bestSeg)], route[static_cast<size_t>(bestSeg) + 1], bestT));
    for (size_t i = static_cast<size_t>(bestSeg) + 1; i < route.size(); ++i)
    {
        ahead.push_back(route[i]);
    }

    std::vector<Node> path;
    path.push_back(ahead.front());
    for (size_t i = 1; i < ahead.size(); ++i)
    {
        appendLeg(path, ahead[i - 1], ahead[i]);
    }
    mGroundIncomplete = false;
    if (mTerrain != nullptr)
    {
        for (Node &node : path)
        {
            const float ground = mTerrain->sampleGroundM(node.lat, node.lon);
            if (ground > -500.0f)
            {
                node.alt = ground;
            }
            else
            {
                const glm::dvec2 ne = toNorthEast(frame.latitude, frame.longitude, node.lat, node.lon);
                if (std::hypot(ne.x, ne.y) < 20000.0)
                {
                    mGroundIncomplete = true;
                }
            }
        }
    }

    const float lat = glm::radians(static_cast<float>(frame.latitude));
    const float lon = glm::radians(static_cast<float>(frame.longitude));
    const glm::vec3 east(-std::sin(lon), std::cos(lon), 0.0f);
    const glm::vec3 north(-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat));
    const glm::vec3 geodeticUp = glm::normalize(glm::cross(east, north));
    const float northFwd = glm::dot(frame.forward, north);
    const float eastFwd = glm::dot(frame.forward, east);

    std::vector<Node> front;
    front.reserve(path.size());
    for (const Node &node : path)
    {
        const glm::dvec2 ne = toNorthEast(frame.latitude, frame.longitude, node.lat, node.lon);
        const double along = ne.x * northFwd + ne.y * eastFwd;
        if (along > 40.0)
        {
            front.push_back(node);
        }
    }
    if (front.size() < 2)
    {
        return;
    }

    const float width = ribbonWidthM(frame, geodeticUp, front.front().alt);
    const float half = width * 0.5f;
    const auto origin = GeoCoordUtils::convertLatLonToXYZ(frame.latitude, frame.longitude, 0.0);
    mCenter = glm::dvec3(origin.x, origin.y, origin.z);

    Triangles mesh;
    mesh.material = "route-ground";
    mesh.vertex.reserve(front.size() * 2);
    mesh.indices.reserve((front.size() - 1) * 6);
    for (size_t i = 0; i < front.size(); ++i)
    {
        const Node &node = front[i];
        const Node &next = front[std::min(i + 1, front.size() - 1)];
        const Node &prev = front[i == 0 ? 0 : i - 1];
        const Node &from = i + 1 < front.size() ? node : prev;
        const Node &to = i + 1 < front.size() ? next : node;
        const glm::dvec2 delta = toNorthEast(from.lat, from.lon, to.lat, to.lon);
        const double len = std::max(1.0, std::hypot(delta.x, delta.y));
        const double acrossN = -delta.y / len * half;
        const double acrossE = delta.x / len * half;
        const float alt = node.alt + kGroundBiasM;
        const auto left = GeoCoordUtils::offsetMeters(node.lat, node.lon, acrossN, acrossE);
        const auto right = GeoCoordUtils::offsetMeters(node.lat, node.lon, -acrossN, -acrossE);
        mesh.vertex.push_back(vertexAt(mCenter, left.latitude, left.longitude, alt));
        mesh.vertex.push_back(vertexAt(mCenter, right.latitude, right.longitude, alt));
        if (i + 1 < front.size())
        {
            const unsigned base = static_cast<unsigned>(i * 2);
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base + 1, base + 3, base + 2});
        }
    }

    if (!mColorReady)
    {
        mShader.setColor("route-ground", kMagenta30);
        mColorReady = true;
    }
    mShader.clearGeometry();
    mShader.setTriangles({mesh});
    mReady = true;
    mBuiltLat = frame.latitude;
    mBuiltLon = frame.longitude;
    mBuiltAlt = frame.altitudeM;
    mBuiltForward = frame.forward;
    mBuiltW = frame.screenW;
    mBuiltH = frame.screenH;
    mBuiltRevision = mPlan->revision();
}

void RouteGround::render(const SceneFrame &frame)
{
    bool rebuildNow = needsRebuild(frame);
    if (!rebuildNow && mGroundIncomplete)
    {
        // Tiles may still be loading. Retry occasionally instead of every frame.
        if (++mGroundRetry >= 12)
        {
            mGroundRetry = 0;
            rebuildNow = true;
        }
    }
    else if (rebuildNow)
    {
        mGroundRetry = 0;
    }
    if (rebuildNow)
    {
        rebuild(frame);
    }
    if (!mReady)
    {
        return;
    }
    const glm::vec3 eyeLocal(frame.eye - mCenter);
    const glm::mat4 view = glm::lookAt(eyeLocal, eyeLocal + frame.forward, frame.up);
    mShader.setMvpMatrix(frame.proj * view);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.0f, -2.0f);
    mShader.render();
    glDisable(GL_POLYGON_OFFSET_FILL);
}
