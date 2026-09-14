#ifndef RUNWAY_OVERLAY_H
#define RUNWAY_OVERLAY_H

#include "shader.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

/// Draws runway rectangles on the 3D ground from length, heading, and width.
class RunwayOverlay
{
public:
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

    RunwayOverlay();

    void update(double latitude, double longitude);
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

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
