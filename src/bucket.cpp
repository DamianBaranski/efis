#include "bucket.h"
#include "asset_path.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include "geo_coord_utils.h"
#include <glm/gtc/matrix_transform.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
std::atomic<int> gTerrainLoads{0};
constexpr int kMaxTerrainLoads = 2;
}

Bucket::Bucket(float lat, float lon) : mLon(lon), mLat(lat)
{
    mIndex = genIndex(lat, lon);
    mFilename = AssetPath::resolve("resources/terrain");
    mFilename += "/" + generateTilePath();
    mFilename += "/" + std::to_string(mIndex) + cTileFileExt;
    mShader.enableOpenAipOverlay(true);
}

Bucket::~Bucket()
{
    // Join the loading thread if it's still running
    if (mLoadingThread.joinable())
        mLoadingThread.join();
}

void Bucket::pumpLoad()
{
    uint8_t expected = 0;
    if (!mState.compare_exchange_strong(expected, 1))
    {
        return;
    }
    if (gTerrainLoads.load() >= kMaxTerrainLoads)
    {
        mState.store(0);
        return;
    }
    ++gTerrainLoads;
    std::cout << "Loading terrain " << mFilename << std::endl;
    mLoadingThread = std::thread(&Bucket::loadFile, this, mFilename);
}

bool Bucket::uploadIfReady()
{
    if (mState.load(std::memory_order_acquire) != 2)
    {
        return false;
    }
    mShader.setTriangles(mMesh);
    mState.store(3);
    return true;
}

void Bucket::render()
{
    mShader.render();
}

bool Bucket::contain(float lat, float lon)
{
    long int index = genIndex(lat, lon);
    return mIndex == index;
}

double Bucket::distanceTo(float lat, float lon) const
{
    return GeoCoordUtils::calculateDistance(mLat, mLon, lat, lon);
}

void Bucket::setCamera(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up)
{
    const glm::vec3 eyeLocal(eye - mCenter);
    const glm::mat4 view = glm::lookAt(eyeLocal, eyeLocal + forward, up);
    mShader.setMvpMatrix(proj * view * mModelMat);
}

void Bucket::loadFile(const std::string& filename)
{
    BtgFile btgFile;
    if (btgFile.load(filename)) {
        mMesh = btgFile.generateTriangles();
        // BTG vertices are already offsets from the bounding-sphere center.
        mCenter = glm::dvec3(btgFile.getBoundingSphere().getCenterX(),
                             btgFile.getBoundingSphere().getCenterY(),
                             btgFile.getBoundingSphere().getCenterZ());
        mModelMat = glm::mat4(1.0f);
        appendUnderlay();
        mState.store(2, std::memory_order_release);
    }
    else
    {
        mState.store(4);
    }
    --gTerrainLoads;
}

void Bucket::tileBounds(double &lat0, double &lat1, double &lon0, double &lon1) const
{
    const double span = getSpan(mLat);
    const int baseY = static_cast<int>(std::floor(mLat));
    const int y = static_cast<int>(std::trunc((static_cast<double>(mLat) - baseY) * 8.0));
    const double baseX = std::floor(std::floor(static_cast<double>(mLon) / span) * span);
    const int x = static_cast<int>(std::floor((static_cast<double>(mLon) - baseX) / span));
    lat0 = static_cast<double>(baseY) + y / 8.0;
    lat1 = static_cast<double>(baseY) + (y + 1) / 8.0;
    lon0 = baseX + static_cast<double>(x) * span;
    lon1 = baseX + static_cast<double>(x + 1) * span;
}

void Bucket::appendUnderlay()
{
    double lat0 = 0.0;
    double lat1 = 0.0;
    double lon0 = 0.0;
    double lon1 = 0.0;
    tileBounds(lat0, lat1, lon0, lon1);

    const double midLat = 0.5 * (lat0 + lat1);
    const double metersPerDegLat = 111320.0;
    const double metersPerDegLon =
        std::max(1000.0, metersPerDegLat * std::cos(midLat * (M_PI / 180.0)));
    constexpr double kOverlapM = 40.0;
    constexpr double kDropM = 10.0;
    constexpr double kSkirtM = 40.0;
    lat0 -= kOverlapM / metersPerDegLat;
    lat1 += kOverlapM / metersPerDegLat;
    lon0 -= kOverlapM / metersPerDegLon;
    lon1 += kOverlapM / metersPerDegLon;

    const auto centreLl = GeoCoordUtils::convertXYZToLatLon(mCenter.x, mCenter.y, mCenter.z);
    const auto onEllipsoid =
        GeoCoordUtils::convertLatLonToXYZ(centreLl.latitude, centreLl.longitude, 0.0);
    const double groundAlt =
        std::sqrt(mCenter.x * mCenter.x + mCenter.y * mCenter.y + mCenter.z * mCenter.z) -
        std::sqrt(onEllipsoid.x * onEllipsoid.x + onEllipsoid.y * onEllipsoid.y +
                  onEllipsoid.z * onEllipsoid.z);
    const double patchAlt = groundAlt - kDropM;

    auto makeVt = [&](double lat, double lon, double alt) {
        const auto xyz = GeoCoordUtils::convertLatLonToXYZ(lat, lon, alt);
        VertexTexture vt{};
        vt.vertex.x = static_cast<float>(xyz.x - mCenter.x);
        vt.vertex.y = static_cast<float>(xyz.y - mCenter.y);
        vt.vertex.z = static_cast<float>(xyz.z - mCenter.z);
        GeoCoordUtils::latLonToMercatorUv(lat, lon, vt.geoCoord.x, vt.geoCoord.y);
        return vt;
    };

    constexpr int kDiv = 8;
    Triangles patch;
    patch.material = mMesh.empty() ? AssetPath::resolve("resources/textures/unknown.png") : mMesh.front().material;
    patch.vertex.reserve(static_cast<size_t>((kDiv + 1) * (kDiv + 1) + 4 * (kDiv + 1)));
    for (int j = 0; j <= kDiv; ++j)
    {
        const double lat = lat0 + (lat1 - lat0) * (static_cast<double>(j) / kDiv);
        for (int i = 0; i <= kDiv; ++i)
        {
            const double lon = lon0 + (lon1 - lon0) * (static_cast<double>(i) / kDiv);
            patch.vertex.push_back(makeVt(lat, lon, patchAlt));
        }
    }
    auto idxAt = [](int i, int j) { return static_cast<unsigned int>(j * (kDiv + 1) + i); };
    for (int j = 0; j < kDiv; ++j)
    {
        for (int i = 0; i < kDiv; ++i)
        {
            const unsigned int a = idxAt(i, j);
            const unsigned int b = idxAt(i + 1, j);
            const unsigned int c = idxAt(i + 1, j + 1);
            const unsigned int d = idxAt(i, j + 1);
            patch.indices.push_back(a);
            patch.indices.push_back(b);
            patch.indices.push_back(c);
            patch.indices.push_back(a);
            patch.indices.push_back(c);
            patch.indices.push_back(d);
        }
    }

    auto addSkirt = [&](int i0, int j0, int i1, int j1) {
        const double latA = lat0 + (lat1 - lat0) * (static_cast<double>(j0) / kDiv);
        const double lonA = lon0 + (lon1 - lon0) * (static_cast<double>(i0) / kDiv);
        const double latB = lat0 + (lat1 - lat0) * (static_cast<double>(j1) / kDiv);
        const double lonB = lon0 + (lon1 - lon0) * (static_cast<double>(i1) / kDiv);
    const unsigned int topA = idxAt(i0, j0);
    const unsigned int topB = idxAt(i1, j1);
    const unsigned int botA = static_cast<unsigned int>(patch.vertex.size());
    patch.vertex.push_back(makeVt(latA, lonA, patchAlt - kSkirtM));
    const unsigned int botB = static_cast<unsigned int>(patch.vertex.size());
    patch.vertex.push_back(makeVt(latB, lonB, patchAlt - kSkirtM));
    patch.indices.push_back(topA);
    patch.indices.push_back(topB);
    patch.indices.push_back(botB);
    patch.indices.push_back(topA);
    patch.indices.push_back(botB);
    patch.indices.push_back(botA);
};
    for (int i = 0; i < kDiv; ++i)
    {
        addSkirt(i, 0, i + 1, 0);
        addSkirt(i, kDiv, i + 1, kDiv);
    }
    for (int j = 0; j < kDiv; ++j)
    {
        addSkirt(0, j, 0, j + 1);
        addSkirt(kDiv, j, kDiv, j + 1);
    }

    mMesh.insert(mMesh.begin(), std::move(patch));
}

std::string Bucket::generateTilePath()
{
    int top_lon, top_lat, main_lon, main_lat;
    char hem, pole;

    top_lon = static_cast<int>(mLon / 10);
    main_lon = static_cast<int>(mLon);
    if ((mLon < 0) && (top_lon * 10 != mLon))
    {
        top_lon -= 1;
    }
    top_lon *= 10;
    if (top_lon >= 0)
    {
        hem = 'e';
    }
    else
    {
        hem = 'w';
        top_lon *= -1;
    }
    if (main_lon < 0)
    {
        main_lon *= -1;
    }

    top_lat = static_cast<int>(mLat / 10);
    main_lat = static_cast<int>(mLat);
    if ((mLat < 0) && (top_lat * 10 != mLat))
    {
        top_lat -= 1;
    }
    top_lat *= 10;
    if (top_lat >= 0)
    {
        pole = 'n';
    }
    else
    {
        pole = 's';
        top_lat *= -1;
    }
    if (main_lat < 0)
    {
        main_lat *= -1;
    }

    std::stringstream ss;
    ss << std::setw(3) << std::setfill('0') << top_lon;
    std::string top_lon_str = ss.str();
    ss.str(""); // Clear stringstream
    ss << std::setw(2) << std::setfill('0') << top_lat;
    std::string top_lat_str = ss.str();
    ss.str(""); // Clear stringstream
    ss << std::setw(3) << std::setfill('0') << main_lon;
    std::string main_lon_str = ss.str();
    ss.str(""); // Clear stringstream
    ss << std::setw(2) << std::setfill('0') << main_lat;
    std::string main_lat_str = ss.str();
    ss.str(""); // Clear stringstream

    return std::string(1, hem) + top_lon_str + pole + top_lat_str + "/" +
           std::string(1, hem) + main_lon_str + pole + main_lat_str;
}

long int Bucket::genIndex(float lat, float lon)
{
    // Calculate the span
    double span = getSpan(lat);
    int base_y = floor(lat);
    int y = trunc((lat - base_y) * 8);

    int base_x = floor(floor(lon / span) * span);
    int x = floor((lon - base_x) / span);

    long int index = (((long int)lon + 180) << 14) + (((long int)lat + 90) << 6) + (y << 3) + x;
    return index;
}

double Bucket::getSpan(double l)
{
    if (l >= 89.0)
    {
        return 12.0;
    }
    else if (l >= 86.0)
    {
        return 4.0;
    }
    else if (l >= 83.0)
    {
        return 2.0;
    }
    else if (l >= 76.0)
    {
        return 1.0;
    }
    else if (l >= 62.0)
    {
        return 0.5;
    }
    else if (l >= 22.0)
    {
        return 0.25;
    }
    else if (l >= -22.0)
    {
        return 0.125;
    }
    else if (l >= -62.0)
    {
        return 0.25;
    }
    else if (l >= -76.0)
    {
        return 0.5;
    }
    else if (l >= -83.0)
    {
        return 1.0;
    }
    else if (l >= -86.0)
    {
        return 2.0;
    }
    else if (l >= -89.0)
    {
        return 4.0;
    }
    else
    {
        return 12.0;
    }
}
