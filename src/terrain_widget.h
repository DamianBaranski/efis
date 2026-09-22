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
#include "openaip_atlas.h"
#include "openaip_client.h"
#include "airspace_overlay.h"
#include "runway_overlay.h"
#include "vrp_overlay.h"
#include "asset_path.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <GLES3/gl3.h>
#include <glm/gtc/matrix_transform.hpp>

class TerrainWidget : public IWidget, public IObserver<DataType>
{
public:
    TerrainWidget(Screen &screen, IDataManager &dataManager) : IWidget(screen), mDataManager(dataManager)
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

    void setSatelliteGround(bool enable) { mSatelliteGround = enable; }

    void setChartOverlay(bool enable) { mChartOverlay = enable; }

    void setAirspacesEnabled(bool enable) { mAirspacesEnabled = enable; }

    bool airspacesEnabled() const { return mAirspacesEnabled; }

    void pumpMapPreload()
    {
        mLocation = mDataManager.getLocationData();
        if (mLocation.latitude == 0.0f && mLocation.longitude == 0.0f)
        {
            mLocation.latitude = 50.959167f;
            mLocation.longitude = 16.770278f;
            mLocation.altitude = 800.0f;
        }
        mOpenAipNear.setLayers(true, mChartOverlay);
        mOpenAipFar.setLayers(true, mChartOverlay);
        if (!mOpenAipNear.ready())
        {
            mOpenAipNear.pump(mLocation.latitude, mLocation.longitude, 8);
        }
        else
        {
            mOpenAipFar.pump(mLocation.latitude, mLocation.longitude, 6);
        }
    }

    OpenAipAtlas::Progress nearPreload() const { return mOpenAipNear.progress(); }
    OpenAipAtlas::Progress farPreload() const { return mOpenAipFar.progress(); }

    void update(DataType type) override {
        if(type != DataType::LOCATION_DATA) {
            return;
        }
        mLocation = mDataManager.getLocationData();
        OpenAipClient::instance().fetchAround(mLocation.latitude, mLocation.longitude, OpenAipAtlas::kDetailZoom,
                                              OpenAipAtlas::kDetailRadius);
        OpenAipClient::instance().fetchAround(mLocation.latitude, mLocation.longitude, OpenAipAtlas::kWideZoom,
                                              OpenAipAtlas::kWideRadius);
        if (!mLoggedPosition)
        {
            std::cout << "Terrain camera " << mLocation.latitude << " N, "
                      << mLocation.longitude << " E, alt " << mLocation.altitude << " m" << std::endl;
            mLoggedPosition = true;
        }
    }

    virtual void render()
    {
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
        mMap.updateLocation(mLocation.latitude, mLocation.longitude);
        glm::dvec3 eye;
        glm::vec3 forward;
        glm::vec3 up;
        glm::vec3 east;
        glm::vec3 geodeticUp;
        buildCamera(eye, forward, up, east, geodeticUp);

        const bool drape = mSatelliteGround || mChartOverlay;
        if (drape)
        {
            Shader::setOpenAipGround(true, mOpenAipNear.texture(),
                                     mOpenAipNear.originX(), mOpenAipNear.originY(),
                                     mOpenAipNear.tilesX(), mOpenAipNear.tilesY(),
                                     mOpenAipNear.n(), mOpenAipFar.texture(),
                                     mOpenAipFar.originX(), mOpenAipFar.originY(),
                                     mOpenAipFar.tilesX(), mOpenAipFar.tilesY(),
                                     mOpenAipFar.n());
        }
        else
        {
            Shader::setOpenAipGround(false, 0, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
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
        mSkybox.setMvpMatrix(mProjMat * skyView * skyModel);
        mSkybox.render();
        glDepthMask(GL_TRUE);

        mMap.render(mProjMat, eye, forward, up);
        mRunways.update(mLocation.latitude, mLocation.longitude);
        mRunways.render(mProjMat, eye, forward, up);
        if (mAirspacesEnabled)
        {
            mAirspaces.update(mLocation.latitude, mLocation.longitude, mLocation.altitude, mProjMat, eye, forward, up,
                              mScreen.getWidth(), mScreen.getHeight());
            mAirspaces.render(mProjMat, eye, forward, up);
        }
        mVrps.update(mLocation.latitude, mLocation.longitude);
        mVrps.render(mProjMat, eye, forward, up);
        Shader::setOpenAipGround(false, 0, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        glEnable(GL_BLEND);
    }

    virtual void setPos(int x, int y)
    {
        (void)x;
        (void)y;
    }

private:
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

        const glm::vec3 along = glm::normalize(north * std::cos(heading) + east * std::sin(heading));
        const glm::vec3 right = glm::normalize(east * std::cos(heading) - north * std::sin(heading));
        forward = glm::normalize(along * std::cos(pitch) + geodeticUp * std::sin(pitch));
        const glm::vec3 upPitched = glm::normalize(-along * std::sin(pitch) + geodeticUp * std::cos(pitch));
        up = glm::normalize(upPitched * std::cos(roll) - right * std::sin(roll));
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
    bool mAirspacesEnabled = true;
    OpenAipAtlas mOpenAipNear{OpenAipAtlas::kDetailZoom, OpenAipAtlas::kDetailRadius};
    OpenAipAtlas mOpenAipFar{OpenAipAtlas::kWideZoom, OpenAipAtlas::kWideRadius};
    RunwayOverlay mRunways;
    AirspaceOverlay mAirspaces;
    VrpOverlay mVrps;
};

#endif