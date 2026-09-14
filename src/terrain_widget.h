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
        mLocation = {};
        initSkybox();
        mProjMat = glm::perspective(glm::radians(60.0f), (float)mScreen.getWidth() / mScreen.getHeight(), 10.0f, 250000.0f);
    }

    void update(DataType type) override {
        if(type != DataType::LOCATION_DATA) {
            return;
        }
        mLocation = mDataManager.getLocationData();
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
        mMap.updateLocation(mLocation.latitude, mLocation.longitude);
        glm::dvec3 eye;
        glm::vec3 forward;
        glm::vec3 up;
        buildCamera(eye, forward, up);

        const glm::mat4 skyView = glm::mat4(glm::mat3(glm::lookAt(glm::vec3(0.0f), forward, up)));
        glDisable(GL_BLEND);
        glDepthMask(GL_FALSE);
        mSkybox.setMvpMatrix(mProjMat * skyView);
        mSkybox.render();
        glDepthMask(GL_TRUE);

        mMap.render(mProjMat, eye, forward, up);
        glEnable(GL_BLEND);
    }

    virtual void setPos(int x, int y)
    {
        (void)x;
        (void)y;
    }

private:
    void buildCamera(glm::dvec3 &eye, glm::vec3 &forward, glm::vec3 &up) const
    {
        const AttitudeData attitude = mDataManager.getAttitudeData();
        const float lat = glm::radians(mLocation.latitude);
        const float lon = glm::radians(mLocation.longitude);
        const float alt = std::max(50.0f, mLocation.altitude);
        GeoCoordUtils::XYZ xyz = GeoCoordUtils::convertLatLonToXYZ(mLocation.latitude, mLocation.longitude, alt);
        eye = glm::dvec3(xyz.x, xyz.y, xyz.z);

        const glm::vec3 east(-std::sin(lon), std::cos(lon), 0.0f);
        const glm::vec3 north(-std::sin(lat) * std::cos(lon), -std::sin(lat) * std::sin(lon), std::cos(lat));
        const glm::vec3 geodeticUp = glm::normalize(glm::cross(east, north));

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
        std::vector<Triangles> trianglesVector;
        float skySize = 50000.0;
        Triangles triangles;
        // Walls
        triangles.material = "../resources/textures/skybox/wall.png";
        triangles.vertex = {{{-skySize, -skySize, skySize}, {0.0, 0.999}},
                            {{skySize, -skySize, skySize}, {1.0, 0.999}},
                            {{skySize, skySize, skySize}, {1.0, 0.001}},
                            {{-skySize, skySize, skySize}, {0.0, 0.001}},

                            {{-skySize, -skySize, -skySize}, {1.0, 0.999}},
                            {{skySize, -skySize, -skySize}, {0.0, 0.999}},
                            {{skySize, skySize, -skySize}, {0.0, 0.001}},
                            {{-skySize, skySize, -skySize}, {1.0, 0.001}}

        };
        triangles.indices = {
            0,
            1,
            2,
            2,
            3,
            0,
            4,
            5,
            6,
            6,
            7,
            4,
            1,
            2,
            5,
            5,
            6,
            2,
            0,
            3,
            4,
            4,
            7,
            3,
        };
        trianglesVector.push_back(triangles);
        // Top
        triangles.material = "../resources/textures/skybox/top.png";
        triangles.vertex = {{{skySize, skySize, skySize}, {1.0, 1.0}},
                            {{-skySize, skySize, skySize}, {0.0, 1.0}},
                            {{skySize, skySize, -skySize}, {0.0, 1.0}},
                            {{-skySize, skySize, -skySize}, {1.0, 1.0}}};
        triangles.indices = {0, 1, 2, 1, 2, 3};
        trianglesVector.push_back(triangles);
        // Bottom
        triangles.material = "../resources/textures/skybox/bottom.png";
        triangles.vertex = {{{skySize, -skySize, skySize}, {1.0, 1.0}},
                            {{-skySize, -skySize, skySize}, {0.0, 1.0}},
                            {{skySize, -skySize, -skySize}, {0.0, 1.0}},
                            {{-skySize, -skySize, -skySize}, {1.0, 1.0}}};
        triangles.indices = {0, 1, 2, 1, 2, 3};
        trianglesVector.push_back(triangles);

        mSkybox.setTriangles(trianglesVector);
    }
    mutable Shader mSkybox;
    glm::mat4 mProjMat;
    mutable BucketContainer mMap;
    IDataManager &mDataManager;
    LocationData mLocation;
    bool mLoggedPosition = false;
};

#endif