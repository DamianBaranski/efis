/// \file airport_scenery.h
/// Airport BTG meshes that fill holes in the terrain tiles.
#ifndef AIRPORT_SCENERY_H
#define AIRPORT_SCENERY_H

#include "shader.h"
#include <atomic>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

/// Loads FlightGear airport BTG meshes (ICAO.btg.gz) that fill holes in terrain tiles.
class AirportScenery
{
public:
    /// Queues airport meshes whose fields are near the camera.
    void update(float latitude, float longitude);
    /// Draws airport meshes that have finished loading.
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

private:
    struct Model
    {
        std::string path;
        Shader shader;
        glm::dvec3 center{0.0};
        glm::mat4 modelMat{1.0f};
        std::vector<Triangles> mesh;
        std::thread loader;
        std::atomic<bool> ready{false};
        bool uploaded = false;

        ~Model()
        {
            if (loader.joinable())
            {
                loader.join();
            }
        }
    };

    static std::string cellPath(double latitude, double longitude);
    static bool isAirportBtg(const std::string &name);
    void loadFile(Model *model);
    void scanCell(double latitude, double longitude, std::vector<std::string> &wanted);

    std::unordered_map<std::string, std::unique_ptr<Model>> mAirports;
    int mScanLat = -1000;
    int mScanLon = -1000;
};

#endif
