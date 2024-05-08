#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "data_type.h"
#include "shader.h"
#include "render2d.h"
#include "bucket_container.h"
#include <cmath>

class GeoConverter
{
private:
    const double a = 6378137.0;                  // semi-major axis of the Earth (in meters)
    const double f_inv = 298.257223563;          // inverse of flattening
    const double f = 1.0 / f_inv;                // flattening
    const double b = a * (1.0 - f);              // semi-minor axis of the Earth
    const double e_sq = 1.0 - (b * b) / (a * a); // eccentricity squared

public:
    struct XYZ
    {
        double x;
        double y;
        double z;
    };

    XYZ convertLatLonToXYZ(double lat, double lon, double alt=0)
    {
        lat = degToRad(lat);
        lon = degToRad(lon);

        double N = a / std::sqrt(1.0 - e_sq * std::sin(lat) * std::sin(lat));

        double x = (N + alt) * std::cos(lat) * std::cos(lon);
        double y = (N + alt) * std::cos(lat) * std::sin(lon);
        double z = (N * (1.0 - e_sq) + alt) * std::sin(lat);

        return {x, y, z};
    }

private:
    double degToRad(double deg)
    {
        return deg * M_PI / 180.0;
    }
};

class TerrainWidget : public IWidget, public IObserver<DataType>
{
public:
    TerrainWidget(Screen &screen, IDataManager &dataManager) : IWidget(screen), mDataManager(dataManager)
    {
        mDataManager.attach(this, DataType::LOCATION_DATA);
        mCamAngle = 8.1; // 1.6;
        initSkybox();
        mProjMat = glm::perspective(glm::radians(60.0f), (float)mScreen.getWidth() / mScreen.getHeight(), 1.0f, 1000000000.0f);
    }

    void update(DataType type) override {
        if(type != DataType::LOCATION_DATA) {
            return;
        }
        mLocation = mDataManager.getLocationData();
        float lat = mDataManager.getLocationData().latitude;
        float lon = mDataManager.getLocationData().longitude;
        GeoConverter converter;
        GeoConverter::XYZ xyz = converter.convertLatLonToXYZ(lat, lon, 1000);
        mViewMat = glm::translate(glm::mat4(1.0f), glm::vec3(xyz.x, xyz.y, xyz.z));
        mSkyboxModelMat = glm::translate(glm::mat4(1.0), glm::vec3(-xyz.x, -xyz.y, -xyz.z));

        //std::cout << std::fixed << "Converter: x:" << xyz.x << " y:" << xyz.y << " z:" << xyz.z << std::endl;
    }

    virtual void render()
    {
        mMap.updateLocation(mLocation.latitude, mLocation.longitude);
        glm::mat4 camRotoation = glm::rotate(glm::mat4(1.0), mCamAngle, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::mat4 mvpMat = mProjMat * camRotoation * mViewMat;

        mMap.render(mvpMat);

        glm::mat4 skyboxMvpMat = mProjMat * mViewMat * mSkyboxModelMat;
        mSkybox.setMvpMatrix(skyboxMvpMat);
        mSkybox.render();
        //mCamAngle += 0.005;
     //std::cout << mCamAngle << std::endl;
    }

    virtual void setPos(int x, int y)
    {
        (void)x;
        (void)y;
    }

private:
    void initSkybox()
    {
        std::vector<Triangles> trianglesVector;
        float skySize = 1000000.0;
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
    mutable float mCamAngle;
    glm::mat4 mViewMat;
    glm::mat4 mProjMat;
    glm::mat4 mSkyboxModelMat;
    mutable BucketContainer mMap;
    IDataManager &mDataManager;
    LocationData mLocation;
};