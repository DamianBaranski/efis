/// \file obstacle_overlay.h
/// Obstacle masts drawn from OpenAIP GeoJSON.
#ifndef OBSTACLE_OVERLAY_H
#define OBSTACLE_OVERLAY_H

#include "iscene_layer.h"
#include "shader.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_set>
#include <vector>

/// Draws nearby OpenAIP obstacles (wind turbines, chimneys, towers) as masts.
class ObstacleOverlay : public ISceneLayer
{
public:
    /// Empty overlay. The Czech and Polish catalogs are read on the first update.
    ObstacleOverlay();

    /// Rebuilds the nearby list when the aircraft has moved about 15 km.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    void update(double latitude, double longitude);
    /// Draws masts and plates. Unknown height draws a short mast and no number.
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

    void update(const SceneFrame &frame) override { update(frame.latitude, frame.longitude); }
    void render(const SceneFrame &frame) override
    {
        render(frame.proj, frame.eye, frame.forward, frame.up);
    }

    /// Obstacle family used for color, mast width, and the spoken phrase.
    enum class Kind
    {
        Wind,
        Chimney,
        Tower,
        Building,
        Other,
    };

    /// One obstacle the voice logic may speak.
    struct Cue
    {
        std::string name;
        double latitude = 0.0;
        double longitude = 0.0;
        float heightM = 0.0f;
        Kind kind = Kind::Other;
    };

    /// Nearby obstacles, nearest first. Height is metres AGL, 0 when unknown.
    const std::vector<Cue> &nearby() const { return mNearby; }

private:
    struct Point
    {
        std::string name;
        std::string label;
        double latitude = 0.0;
        double longitude = 0.0;
        float elevationM = 0.0f;
        float heightM = 0.0f;
        Kind kind = Kind::Other;
    };

    void loadCatalog();
    void loadGeoJson(const std::string &path);
    void rebuild(double latitude, double longitude);
    void appendMast(Triangles &out, const Point &point) const;
    void appendLabel(std::vector<Triangles> &mesh, const Point &point);
    void addGroundFan(Triangles &out, double lat, double lon, float elevM, float radiusM, int sides) const;
    void addVerticalQuad(Triangles &out, double lat, double lon, float altMid, float northHat, float eastHat,
                         float widthM, float heightM, bool flipU) const;
    bool ensureLabelTexture(const std::string &label);

    Shader mMarks;
    Shader mLabels;
    glm::dvec3 mCenter{0.0};
    bool mReady = false;
    std::vector<Point> mCatalog;
    std::vector<Cue> mNearby;
    std::unordered_set<std::string> mReadyLabels;
    double mBuiltLat = 0.0;
    double mBuiltLon = 0.0;
};

#endif
