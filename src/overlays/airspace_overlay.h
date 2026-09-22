/// \file airspace_overlay.h
/// Airspace walls and name plates drawn from OpenAIP GeoJSON.
#ifndef AIRSPACE_OVERLAY_H
#define AIRSPACE_OVERLAY_H

#include "shader.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// Draws nearby OpenAIP vector airspaces as translucent vertical walls (no floor or ceiling).
class AirspaceOverlay
{
public:
    AirspaceOverlay();

    /// Rebuilds walls near the aircraft and notes which volume is current.
    /// \param altitudeM Metres above the ellipsoid.
    void update(double latitude, double longitude, float altitudeM, const glm::mat4 &proj, const glm::dvec3 &eye,
                const glm::vec3 &forward, const glm::vec3 &up, int screenW, int screenH);
    /// Draws the walls and the name plates that are enabled.
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

    /// Draws the vertical walls. Independent of the name plates.
    void setDrawWalls(bool enable) { mDrawWalls = enable; }
    /// Draws the name plates. Independent of the walls.
    void setDrawLabels(bool enable) { mDrawLabels = enable; }

    struct Point
    {
        double lat = 0.0;
        double lon = 0.0;
    };

    struct Volume
    {
        std::string name;
        int type = 0;
        double centLat = 0.0;
        double centLon = 0.0;
        double minLat = 0.0;
        double maxLat = 0.0;
        double minLon = 0.0;
        double maxLon = 0.0;
        float lowerM = 0.0f;
        float upperM = 0.0f;
        std::string lowerLabel;
        std::string upperLabel;
        std::vector<Point> ring;
    };

private:
    struct AirportElev
    {
        double lat = 0.0;
        double lon = 0.0;
        float elevM = 151.0f;
    };

    enum class Transit
    {
        None,
        Inbound,
        Outbound,
        Below,
        Above
    };

    struct LabelState
    {
        Transit kind = Transit::None;
        int distTenths = -1;
        double lastDistM = -1.0;
        double s = 0.0;
        double lastDrawnS = -1.0e9;
        bool sInit = false;
    };

    void loadAirportElev();
    void loadCatalog();
    void loadGeoJson(const std::string &path);
    void dropDuplicateRmz();
    void rebuild(double latitude, double longitude);
    void refreshLabels();
    void refreshLabelTextures();
    void updateInside(double latitude, double longitude, float altitudeM, const glm::mat4 &proj, const glm::dvec3 &eye,
                      const glm::vec3 &forward, const glm::vec3 &up, int screenW, int screenH);
    float nearestGroundM(double lat, double lon) const;
    bool isNearby(const Volume &volume, double latitude, double longitude) const;
    std::string captionFor(const Volume &volume) const;
    void appendWalls(Triangles &out, const Volume &volume, const glm::dvec3 &center) const;
    void appendLabels(std::vector<Triangles> &mesh, const Volume &volume);
    bool ensureLabelTexture(const std::string &material, const std::string &label, SDL_Color color);
    void addLabelQuad(Triangles &out, double lat, double lon, float altM, float northHat, float eastHat, float widthM,
                      float heightM, bool flipU, bool frontOutward);

    Shader mShader;
    Shader mLabels;
    bool mDrawWalls = true;
    bool mDrawLabels = true;
    glm::dvec3 mCenter{0.0};
    bool mReady = false;
    std::vector<AirportElev> mAirports;
    std::vector<Volume> mCatalog;
    std::vector<Volume> mNearby;
    std::unordered_set<std::string> mInside;
    std::unordered_map<std::string, LabelState> mLabelState;
    std::unordered_map<std::string, std::string> mDrawnCaption;
    double mBuiltLat = 0.0;
    double mBuiltLon = 0.0;
    uint32_t mLastFollowMs = 0;
};

#endif
