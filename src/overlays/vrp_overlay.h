/// \file vrp_overlay.h
/// Visual reporting points drawn from the OpenAIP catalog.
#ifndef VRP_OVERLAY_H
#define VRP_OVERLAY_H

#include "iscene_layer.h"
#include "shader.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_set>
#include <vector>

/// Draws nearby OpenAIP visual reporting points as ground marks plus name plates.
class VrpOverlay : public ISceneLayer
{
public:
    /// Empty overlay. The reporting-point catalog is read on the first update.
    VrpOverlay();

    /// Rebuilds marks near the aircraft.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    void update(double latitude, double longitude);
    /// Draws the ground marks and the name plates.
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

    void update(const SceneFrame &frame) override { update(frame.latitude, frame.longitude); }
    void render(const SceneFrame &frame) override
    {
        render(frame.proj, frame.eye, frame.forward, frame.up);
    }

    /// One reporting point from the catalog.
    struct Point
    {
        std::string name;
        double latitude = 0.0;
        double longitude = 0.0;
        float elevationM = 151.0f;
        bool compulsory = false;
    };

    /// Reporting points currently in range.
    const std::vector<Point> &nearby() const { return mNearby; }

private:
    void loadCatalog();
    void loadJson(const std::string &path);
    void rebuild(double latitude, double longitude);
    void appendMark(Triangles &out, const Point &point) const;
    void appendMast(Triangles &out, const Point &point) const;
    void appendLabel(std::vector<Triangles> &mesh, const Point &point);
    void addGroundFan(Triangles &out, double lat, double lon, float elevM, float radiusM, int sides) const;
    void addVerticalQuad(Triangles &out, double lat, double lon, float altMid, float northHat, float eastHat,
                         float widthM, float heightM, bool flipU) const;
    bool ensureLabelTexture(const std::string &name);

    Shader mMarks;
    Shader mLabels;
    glm::dvec3 mCenter{0.0};
    bool mReady = false;
    std::vector<Point> mCatalog;
    std::vector<Point> mNearby;
    std::unordered_set<std::string> mReadyLabels;
    double mBuiltLat = 0.0;
    double mBuiltLon = 0.0;
};

#endif
