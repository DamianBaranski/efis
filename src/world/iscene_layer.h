/// \file iscene_layer.h
/// A 3D layer updated from the aircraft and drawn with the camera.
#ifndef ISCENE_LAYER_H
#define ISCENE_LAYER_H

#include <glm/glm.hpp>

/// One frame of the 3D world. Layers read the fields they need.
struct SceneFrame
{
    double latitude = 0.0;  ///< Degrees, north positive.
    double longitude = 0.0; ///< Degrees, east positive.
    float altitudeM = 0.0f; ///< Metres above the ellipsoid.
    glm::mat4 proj{1.0f};
    glm::dvec3 eye{0.0};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    int screenW = 0; ///< Pixels.
    int screenH = 0; ///< Pixels.
};

/// Terrain, airport meshes, and the airspace, point, and runway drawings.
class ISceneLayer
{
public:
    virtual ~ISceneLayer() = default;

    /// Refresh geometry around the aircraft. Camera fields may be unused.
    virtual void update(const SceneFrame &frame) = 0;

    /// Draw with the camera in the frame.
    virtual void render(const SceneFrame &frame) = 0;
};

#endif
