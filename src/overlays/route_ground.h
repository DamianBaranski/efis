/// \file route_ground.h
/// Flight-plan ribbon drawn on the ground in the 3D view.

#ifndef ROUTE_GROUND_H
#define ROUTE_GROUND_H

#include "iscene_layer.h"
#include "shader.h"
#include <cstdint>

class FlightPlan;
class BucketContainer;

/// Magenta path ahead of the aircraft. Its width grows with height so the
/// near end stays about one seventh of the screen.
class RouteGround : public ISceneLayer
{
public:
    RouteGround();

    /// Shared plan. Not owned. Null hides the ribbon.
    void setFlightPlan(const FlightPlan *plan) { mPlan = plan; }
    void setTerrain(const BucketContainer *terrain) { mTerrain = terrain; }

    void update(const SceneFrame &frame) override;
    void render(const SceneFrame &frame) override;

private:
    void rebuild(const SceneFrame &frame);

    bool needsRebuild(const SceneFrame &frame) const;

    const FlightPlan *mPlan = nullptr;
    const BucketContainer *mTerrain = nullptr;
    Shader mShader;
    bool mColorReady = false;
    bool mReady = false;
    bool mGroundIncomplete = true;
    int mGroundRetry = 0;
    glm::dvec3 mCenter{0.0};
    glm::vec3 mBuiltForward{0.0f};
    double mBuiltLat = 0.0;
    double mBuiltLon = 0.0;
    float mBuiltAlt = 0.0f;
    int mBuiltW = 0;
    int mBuiltH = 0;
    std::uint64_t mBuiltRevision = 0;
};

#endif
