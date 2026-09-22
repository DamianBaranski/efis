#include "airport_scenery.h"

#include "asset_path.h"
#include "btg_file.h"
#include "geo_coord_utils.h"
#include <cmath>
#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <glm/gtc/matrix_transform.hpp>

std::string AirportScenery::cellPath(double latitude, double longitude)
{
    int topLon = static_cast<int>(longitude / 10);
    int mainLon = static_cast<int>(longitude);
    if (longitude < 0 && topLon * 10 != longitude)
    {
        topLon -= 1;
    }
    topLon *= 10;
    const char hem = (topLon >= 0) ? 'e' : 'w';
    if (topLon < 0)
    {
        topLon *= -1;
    }
    if (mainLon < 0)
    {
        mainLon *= -1;
    }

    int topLat = static_cast<int>(latitude / 10);
    int mainLat = static_cast<int>(latitude);
    if (latitude < 0 && topLat * 10 != latitude)
    {
        topLat -= 1;
    }
    topLat *= 10;
    const char pole = (topLat >= 0) ? 'n' : 's';
    if (topLat < 0)
    {
        topLat *= -1;
    }
    if (mainLat < 0)
    {
        mainLat *= -1;
    }

    std::ostringstream path;
    path << hem << std::setw(3) << std::setfill('0') << topLon << pole << std::setw(2) << std::setfill('0') << topLat
         << '/' << hem << std::setw(3) << std::setfill('0') << mainLon << pole << std::setw(2) << std::setfill('0')
         << mainLat;
    return path.str();
}

bool AirportScenery::isAirportBtg(const std::string &name)
{
    const std::string suffix = ".btg.gz";
    if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        return false;
    }
    const std::string stem = name.substr(0, name.size() - suffix.size());
    if (stem.empty())
    {
        return false;
    }
    for (char c : stem)
    {
        if (c < '0' || c > '9')
        {
            return true;
        }
    }
    return false;
}

void AirportScenery::scanCell(double latitude, double longitude, std::vector<std::string> &wanted)
{
    const std::string dir = AssetPath::resolve("resources/terrain") + "/" + cellPath(latitude, longitude);
    DIR *handle = opendir(dir.c_str());
    if (handle == nullptr)
    {
        return;
    }
    while (const dirent *entry = readdir(handle))
    {
        const std::string name = entry->d_name;
        if (!isAirportBtg(name))
        {
            continue;
        }
        wanted.push_back(dir + "/" + name);
    }
    closedir(handle);
}

void AirportScenery::loadFile(Model *model)
{
    BtgFile btgFile;
    if (!btgFile.load(model->path))
    {
        std::cerr << "Airport scenery failed " << model->path << std::endl;
        return;
    }
    model->mesh = btgFile.generateTriangles();
    model->center = glm::dvec3(btgFile.getBoundingSphere().getCenterX(), btgFile.getBoundingSphere().getCenterY(),
                               btgFile.getBoundingSphere().getCenterZ());
    model->modelMat = glm::mat4(1.0f);
    std::cout << "Airport scenery loaded " << model->path << " tris-groups " << model->mesh.size() << std::endl;
    model->ready.store(true, std::memory_order_release);
}

void AirportScenery::update(float latitude, float longitude)
{
    std::vector<std::string> wanted;
    const int lat0 = static_cast<int>(std::floor(latitude - 1.0f));
    const int lat1 = static_cast<int>(std::floor(latitude + 1.0f));
    const int lon0 = static_cast<int>(std::floor(longitude - 1.0f));
    const int lon1 = static_cast<int>(std::floor(longitude + 1.0f));
    for (int lat = lat0; lat <= lat1; ++lat)
    {
        for (int lon = lon0; lon <= lon1; ++lon)
        {
            scanCell(static_cast<double>(lat) + 0.5, static_cast<double>(lon) + 0.5, wanted);
        }
    }

    std::unordered_map<std::string, bool> keep;
    for (const auto &path : wanted)
    {
        keep[path] = true;
        if (mAirports.count(path) != 0)
        {
            continue;
        }
        auto model = std::make_unique<Model>();
        model->path = path;
        model->shader.enableOpenAipOverlay(true);
        Model *raw = model.get();
        std::cout << "Loading airport " << path << std::endl;
        raw->loader = std::thread(&AirportScenery::loadFile, this, raw);
        mAirports.emplace(path, std::move(model));
    }

    auto iter = mAirports.begin();
    while (iter != mAirports.end())
    {
        if (keep.count(iter->first) != 0)
        {
            ++iter;
            continue;
        }
        if (iter->second->loader.joinable())
        {
            iter->second->loader.join();
        }
        iter = mAirports.erase(iter);
    }
}

void AirportScenery::render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up)
{
    for (auto &entry : mAirports)
    {
        Model &model = *entry.second;
        if (model.ready.load(std::memory_order_acquire) && !model.uploaded)
        {
            model.shader.setTriangles(model.mesh);
            model.uploaded = true;
        }
        if (!model.uploaded)
        {
            continue;
        }
        const glm::vec3 eyeLocal(eye - model.center);
        const glm::mat4 view = glm::lookAt(eyeLocal, eyeLocal + forward, up);
        model.shader.setMvpMatrix(proj * view * model.modelMat);
        model.shader.render();
    }
}
