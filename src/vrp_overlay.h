#ifndef VRP_OVERLAY_H
#define VRP_OVERLAY_H

#include "shader.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_set>
#include <vector>

/// Draws nearby OpenAIP visual reporting points as ground marks plus name plates.
class VrpOverlay
{
public:
    VrpOverlay();

    void update(double latitude, double longitude);
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

    struct Point
    {
        std::string name;
        double latitude = 0.0;
        double longitude = 0.0;
        float elevationM = 151.0f;
        bool compulsory = false;
    };

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
