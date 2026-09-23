/// \file terrain_widget.h
/// 3D world: terrain tiles, satellite drape, airspace, points, and obstacles.
#ifndef TERRAIN_WIDGET_H
#define TERRAIN_WIDGET_H

#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "data_type.h"
#include "shader.h"
#include "render2d.h"
#include "bucket_container.h"
#include "geo_coord_utils.h"
#include "sat_clipmap.h"
#include "openaip_client.h"
#include "airspace_overlay.h"
#include "runway_overlay.h"
#include "vrp_overlay.h"
#include "obstacle_overlay.h"
#include "nav_voice.h"
#include "asset_path.h"
#include "iworld_read.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <iostream>
#include <GLES3/gl3.h>
#include <glm/gtc/matrix_transform.hpp>

/// Owns the 3D frame: terrain, satellite drape, airspace, points, and obstacles.
/// The attitude instrument is a separate widget drawn on top.
class TerrainWidget : public IWidget, public IObserver<DataType>, public IWorldRead
{
public:
    /// Subscribes to position and builds the perspective camera.
    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param dataManager Situation source. Must outlive this widget.
    TerrainWidget(Frame &frame, IDataManager &dataManager) : IWidget(frame), mDataManager(dataManager)
    {
        mDataManager.attach(this, DataType::LOCATION_DATA);
        mLocation = mDataManager.getLocationData();
        if (mLocation.latitude == 0.0f && mLocation.longitude == 0.0f)
        {
            mLocation.latitude = 50.959167f;
            mLocation.longitude = 16.770278f;
            mLocation.altitude = 800.0f;
        }
        initSkybox();
        mProjW = mScreen.getWidth();
        mProjH = mScreen.getHeight();
        mProjMat = glm::perspective(glm::radians(60.0f), (float)mProjW / std::max(1, mProjH), 10.0f, 250000.0f);
    }

    /// Drapes Esri imagery on the terrain when enable is true.
    void setSatelliteGround(bool enable) { mSatelliteGround = enable; }

    /// Drapes the OpenAIP chart. The menu currently forces this off.
    void setChartOverlay(bool enable) { mChartOverlay = enable; }

    /// Close-in imagery zoom, from 12 through 18.
    void setSatDetailZoom(int zoom) { mSat.setDetailZoom(zoom); }
    /// Tile count on one side of the close-in ring.
    void setSatNearGrid(int grid) { mSat.setFineGrid(grid); }
    /// Mid-ring imagery zoom.
    void setSatFarZoom(int zoom) { mSat.setMidZoom(zoom); }
    /// Tile count on one side of the mid ring.
    void setSatFarGrid(int grid) { mSat.setMidGrid(grid); }
    /// Camera latitude in degrees, north positive.
    float cameraLatitude() const override { return mLocation.latitude; }
    /// Camera altitude in metres.
    float cameraAltitude() const override { return mLocation.altitude; }

    /// Updates airspace geometry when enable is true.
    void setAirspacesEnabled(bool enable) { mAirspacesEnabled = enable; }
    /// Draws the vertical airspace walls.
    void setAirspaceWalls(bool enable) { mAirspaces.setDrawWalls(enable); }
    /// Draws airspace name plates. Independent of the walls.
    void setAirspaceLabels(bool enable) { mAirspaces.setDrawLabels(enable); }
    /// Draws visual reporting points.
    void setVrpsEnabled(bool enable) { mVrpsEnabled = enable; }
    /// Draws obstacle masts. Voice still runs when this is off.
    void setObstaclesEnabled(bool enable) { mObstaclesEnabled = enable; }

    /// True when airspace geometry is being updated.
    bool airspacesEnabled() const { return mAirspacesEnabled; }

    /// Queues satellite tiles around the current position. At most four uploads.
    void pumpMapPreload()
    {
        mLocation = mDataManager.getLocationData();
        if (mLocation.latitude == 0.0f && mLocation.longitude == 0.0f)
        {
            mLocation.latitude = 50.959167f;
            mLocation.longitude = 16.770278f;
            mLocation.altitude = 800.0f;
        }
        if (!mSatelliteGround && !mChartOverlay)
        {
            return;
        }
        mSat.setChartOverlay(mChartOverlay);
        IImagery &imagery = mSat;
        imagery.pump(mLocation.latitude, mLocation.longitude, 4);
    }

    /// True when the satellite rings are filled, or when no imagery is requested.
    bool mapPreloadReady() const override
    {
        const IImagery &imagery = mSat;
        return (!mSatelliteGround && !mChartOverlay) || imagery.ready();
    }

    /// Tiles finished in the close-in ring.
    SatClipmap::Progress nearPreload() const override { return mSat.fineProgress(); }
    /// Tiles finished in the mid ring.
    SatClipmap::Progress farPreload() const override { return mSat.coarseProgress(); }
    /// Terrain tiles held in memory.
    int terrainLoaded() const override { return mMap.loadedCount(); }
    /// Terrain tiles submitted on the last frame.
    int terrainDrawn() const override { return mMap.drawnCount(); }
    /// GPU bytes held by the satellite rings.
    size_t mapGpuBytes() const override
    {
        const IImagery &imagery = mSat;
        return imagery.gpuBytes();
    }

    /// Rebuilds nearby overlays when the position channel changes.
    void update(DataType type) override {
        if(type != DataType::LOCATION_DATA) {
            return;
        }
        mLocation = mDataManager.getLocationData();
        if (mSatelliteGround || mChartOverlay)
        {
            OpenAipClient::instance().fetchAround(mLocation.latitude, mLocation.longitude, mSat.detailZoom(),
                                                  mSat.fineGrid() / 2, true, mChartOverlay);
            OpenAipClient::instance().fetchAround(mLocation.latitude, mLocation.longitude, mSat.midZoom(),
                                                  mSat.midGrid() / 2, true, mChartOverlay);
        }
        if (!mLoggedPosition)
        {
            std::cout << "Terrain camera " << mLocation.latitude << " N, "
                      << mLocation.longitude << " E, alt " << mLocation.altitude << " m" << std::endl;
            mLoggedPosition = true;
        }
    }

    /// Draws terrain, imagery, and the overlays that are switched on.
    virtual void render()
    {
        pumpMapPreload();
        if (!mEnabled)
        {
            return;
        }
        if (mScreen.getWidth() != mProjW || mScreen.getHeight() != mProjH)
        {
            mProjW = mScreen.getWidth();
            mProjH = mScreen.getHeight();
            mProjMat = glm::perspective(glm::radians(60.0f), (float)mProjW / std::max(1, mProjH), 10.0f, 250000.0f);
        }
        mLocation = mDataManager.getLocationData();
        NavVoice::instance().setPosition(mLocation.latitude, mLocation.longitude);
        SceneFrame ground;
        ground.latitude = mLocation.latitude;
        ground.longitude = mLocation.longitude;
        ground.altitudeM = mLocation.altitude;
        ISceneLayer &terrain = mMap;
        terrain.update(ground);
        glm::dvec3 eye;
        glm::vec3 forward;
        glm::vec3 up;
        glm::vec3 east;
        glm::vec3 geodeticUp;
        buildCamera(eye, forward, up, east, geodeticUp);

        const bool drape = mSatelliteGround || mChartOverlay;
        float camU = 0.0f;
        float camV = 0.0f;
        GeoCoordUtils::latLonToMercatorUv(mLocation.latitude, mLocation.longitude, camU, camV);
        if (drape)
        {
            const SatClipmap::View sat = mSat.view();
            Shader::setSatClip(sat.active, sat.fineTex, sat.midTex, sat.wideTex, sat.fineOriginX, sat.fineOriginY,
                               sat.midOriginX, sat.midOriginY, sat.wideOriginX, sat.wideOriginY, sat.fineZoom,
                               sat.midZoom, sat.wideZoom, sat.fineGrid, sat.fineMask, sat.midGrid, sat.midMask,
                               sat.wideMask0, sat.wideMask1, camU, camV);
        }
        else
        {
            Shader::setSatClip(false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 13, 11, 8, nullptr, 8, nullptr, 0, 0, camU,
                               camV);
        }

        // Cube is Y-up in model space; rotate it onto local ENU so zenith follows geodetic up.
        const glm::vec3 skyNorth = glm::normalize(glm::cross(geodeticUp, east));
        glm::mat4 skyModel(1.0f);
        skyModel[0] = glm::vec4(east, 0.0f);
        skyModel[1] = glm::vec4(geodeticUp, 0.0f);
        skyModel[2] = glm::vec4(-skyNorth, 0.0f);
        const glm::mat4 skyView = glm::mat4(glm::mat3(glm::lookAt(glm::vec3(0.0f), forward, up)));
        glDisable(GL_BLEND);
        glDepthMask(GL_FALSE);
        {
            const float altFt = std::max(0.0f, mLocation.altitude) * 3.280839895f;
            const float t = glm::smoothstep(10000.0f, 25000.0f, altFt);
            const float r = glm::mix(1.0f, 0.12f, t);
            const float g = glm::mix(1.0f, 0.16f, t);
            const float b = glm::mix(1.0f, 0.24f, t);
            mSkybox.setColorScale(r, g, b);
        }
        mSkybox.setMvpMatrix(mProjMat * skyView * skyModel);
        mSkybox.render();
        mSkybox.setColorScale(1.0f, 1.0f, 1.0f);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        SceneFrame frame;
        frame.latitude = mLocation.latitude;
        frame.longitude = mLocation.longitude;
        frame.altitudeM = mLocation.altitude;
        frame.proj = mProjMat;
        frame.eye = eye;
        frame.forward = forward;
        frame.up = up;
        frame.screenW = mScreen.getWidth();
        frame.screenH = mScreen.getHeight();
        terrain.render(frame);
        glDisable(GL_CULL_FACE);
        drawLayer(mRunways, frame);
        if (mAirspacesEnabled)
        {
            drawLayer(mAirspaces, frame);
        }
        if (mVrpsEnabled)
        {
            drawLayer(mVrps, frame);
            NavVoice::instance().updateReporting(mLocation.latitude, mLocation.longitude, mVrps.nearby());
        }
        if (mObstaclesEnabled || NavVoice::instance().obstacles())
        {
            ISceneLayer &obstacles = mObstacles;
            obstacles.update(frame);
            if (mObstaclesEnabled)
            {
                obstacles.render(frame);
            }
            std::vector<NavVoice::ObstacleCue> cues;
            cues.reserve(mObstacles.nearby().size());
            for (const auto &point : mObstacles.nearby())
            {
                NavVoice::ObstacleCue cue;
                cue.name = point.name;
                cue.latitude = point.latitude;
                cue.longitude = point.longitude;
                cue.heightM = point.heightM;
                cue.kind = static_cast<int>(point.kind);
                cues.push_back(std::move(cue));
            }
            NavVoice::instance().updateObstacles(mLocation.latitude, mLocation.longitude, cues);
        }
        Shader::setSatClip(false, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 13, 11, 8, nullptr, 8, nullptr, 0, 0);
        glEnable(GL_BLEND);
    }

    /// Unused. The world fills the window.
    virtual void setPos(int x, int y)
    {
        (void)x;
        (void)y;
    }

private:
    static void drawLayer(ISceneLayer &layer, const SceneFrame &frame)
    {
        layer.update(frame);
        layer.render(frame);
    }

    void buildCamera(glm::dvec3 &eye, glm::vec3 &forward, glm::vec3 &up, glm::vec3 &east,
                     glm::vec3 &geodeticUp) const
    {
        const AttitudeData attitude = mDataManager.getAttitudeData();
        const float lat = glm::radians(mLocation.latitude);
        const float lon = glm::radians(mLocation.longitude);
        const float alt = std::max(50.0f, mLocation.altitude);
        GeoCoordUtils::XYZ xyz = GeoCoordUtils::convertLatLonToXYZ(mLocation.latitude, mLocation.longitude, alt);
        eye = glm::dvec3(xyz.x, xyz.y, xyz.z);

        east = glm::vec3(-std::sin(lon), std::cos(lon), 0.0f);
        const glm::vec3 north(-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat));
        geodeticUp = glm::normalize(glm::cross(east, north));

        const float heading = attitude.heading;
        const float pitch = attitude.pitch;
        const float roll = attitude.roll;

        if (attitude.useQuat)
        {
            // Body FRD to NED. Euler pitch stops at ±90° and roll then jumps 180°,
            // which turns a nose-up tilt into an inverted horizon.
            const auto bodyToNed = [&](float vx, float vy, float vz) {
                const float qw = attitude.qw;
                const float qx = attitude.qx;
                const float qy = attitude.qy;
                const float qz = attitude.qz;
                const float tx = 2.0f * (qy * vz - qz * vy);
                const float ty = 2.0f * (qz * vx - qx * vz);
                const float tz = 2.0f * (qx * vy - qy * vx);
                return glm::vec3(vx + qw * tx + (qy * tz - qz * ty), vy + qw * ty + (qz * tx - qx * tz),
                                 vz + qw * tz + (qx * ty - qy * tx));
            };
            const auto nedToWorld = [&](const glm::vec3 &ned) {
                return glm::normalize(north * ned.x + east * ned.y - geodeticUp * ned.z);
            };
            forward = nedToWorld(bodyToNed(1.0f, 0.0f, 0.0f));
            up = nedToWorld(bodyToNed(0.0f, 0.0f, -1.0f));
            // Nose-up was looking down. Turn about the wing so pitch flips and bank stays.
            const float pitchNow = std::atan2(glm::dot(forward, geodeticUp), glm::dot(up, geodeticUp));
            const glm::vec3 wing = glm::normalize(glm::cross(forward, up));
            const float turn = -2.0f * pitchNow;
            const float c = std::cos(turn);
            const float s = std::sin(turn);
            const auto aboutWing = [&](const glm::vec3 &v) {
                return glm::normalize(v * c + glm::cross(wing, v) * s + wing * glm::dot(wing, v) * (1.0f - c));
            };
            forward = aboutWing(forward);
            up = aboutWing(up);
        }
        else
        {
            const glm::vec3 along = glm::normalize(north * std::cos(heading) + east * std::sin(heading));
            const glm::vec3 right = glm::normalize(east * std::cos(heading) - north * std::sin(heading));
            const float pitched = -pitch;
            forward = glm::normalize(along * std::cos(pitched) + geodeticUp * std::sin(pitched));
            const glm::vec3 upPitched = glm::normalize(-along * std::sin(pitched) + geodeticUp * std::cos(pitched));
            up = glm::normalize(upPitched * std::cos(roll) - right * std::sin(roll));
        }
    }

    void initSkybox()
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSlices = 48;
        constexpr int kStacks = 24;
        const float radius = 50000.0f;
        Triangles triangles;
        triangles.material = AssetPath::resolve("resources/textures/skybox/wall.png");
        triangles.vertex.reserve(static_cast<size_t>(kStacks + 1) * static_cast<size_t>(kSlices + 1));
        for (int stack = 0; stack <= kStacks; ++stack)
        {
            const float phi = static_cast<float>(stack) / static_cast<float>(kStacks) * kPi;
            const float y = std::cos(phi);
            const float ring = std::sin(phi);
            // wall.png is dark at V=0 (zenith) and bright at V=1 (horizon).
            const float v = std::clamp(phi / (0.5f * kPi), 0.001f, 0.999f);
            for (int slice = 0; slice <= kSlices; ++slice)
            {
                const float theta = static_cast<float>(slice) / static_cast<float>(kSlices) * 2.0f * kPi;
                const float x = ring * std::cos(theta);
                const float z = ring * std::sin(theta);
                const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
                VertexTexture vt{};
                vt.vertex.x = x * radius;
                vt.vertex.y = y * radius;
                vt.vertex.z = z * radius;
                vt.textureCoord.x = u;
                vt.textureCoord.y = v;
                triangles.vertex.push_back(vt);
            }
        }
        const unsigned int row = static_cast<unsigned int>(kSlices + 1);
        for (int stack = 0; stack < kStacks; ++stack)
        {
            for (int slice = 0; slice < kSlices; ++slice)
            {
                const unsigned int i0 = static_cast<unsigned int>(stack) * row + static_cast<unsigned int>(slice);
                const unsigned int i1 = i0 + 1;
                const unsigned int i2 = i0 + row;
                const unsigned int i3 = i2 + 1;
                triangles.indices.insert(triangles.indices.end(), {i0, i2, i1, i1, i2, i3});
            }
        }
        mSkybox.setTriangles({triangles});
    }
    mutable Shader mSkybox;
    glm::mat4 mProjMat;
    int mProjW = 0;
    int mProjH = 0;
    mutable BucketContainer mMap;
    IDataManager &mDataManager;
    LocationData mLocation;
    bool mLoggedPosition = false;
    bool mSatelliteGround = false;
    bool mChartOverlay = false;
    bool mAirspacesEnabled = false;
    bool mVrpsEnabled = true;
    bool mObstaclesEnabled = false;
    SatClipmap mSat;
    RunwayOverlay mRunways;
    AirspaceOverlay mAirspaces;
    VrpOverlay mVrps;
    ObstacleOverlay mObstacles;
};

#endif