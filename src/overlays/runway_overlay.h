/// \file runway_overlay.h
/// Runway rectangles drawn on the 3D ground.
#ifndef RUNWAY_OVERLAY_H
#define RUNWAY_OVERLAY_H

#include "iscene_layer.h"
#include "shader.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

/// Draws runway rectangles on the 3D ground from length, heading, and width.
class RunwayOverlay : public ISceneLayer
{
public:
    /// One runway: threshold position, heading, and size.
    struct Strip
    {
        std::string name;
        std::string ident;
        std::string runway;
        std::string runwayRecip;
        double latitude;
        double longitude;
        float headingDeg;
        float lengthM;
        float widthM;
        float elevationM;
    };

    /// Empty overlay. The runway catalog is read on the first update.
    RunwayOverlay();

    /// Rebuilds rectangles near the aircraft.
    void update(double latitude, double longitude);
    /// Draws the rectangles, centerline dashes, and runway numbers.
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

    void update(const SceneFrame &frame) override { update(frame.latitude, frame.longitude); }
    void render(const SceneFrame &frame) override
    {
        render(frame.proj, frame.eye, frame.forward, frame.up);
    }

private:
    void loadCatalog();
    void rebuild(double latitude, double longitude);
    void addRectangle(Triangles &out, double lat, double lon, float headingDeg, float alongM, float lengthM,
                      float widthM, float elevationM, float heightBiasM);
    void addLabelQuad(Triangles &out, double lat, double lon, float headingDeg, float alongM, float lengthM,
                      float widthM, float elevationM, float heightBiasM);
    void addDashes(Triangles &out, const Strip &strip);
    void addNumbers(std::vector<Triangles> &mesh, const Strip &strip);
    bool ensureLabelTexture(const std::string &label);

    Shader mShader;
    glm::dvec3 mCenter{0.0};
    bool mReady = false;
    std::vector<Strip> mCatalog;
    std::vector<Strip> mStrips;
    double mBuiltLat = 0.0;
    double mBuiltLon = 0.0;
};

#endif
