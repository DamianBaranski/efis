/// \file bucket_container.cpp
/// Keeps the terrain tiles inside the camera disk loaded and drawn.
#include "bucket_container.h"
#include "geo_coord_utils.h"
#include <algorithm>
#include <iostream>

namespace
{
constexpr int kAddBucketsPerFrame = 2;
constexpr int kUploadBucketsPerFrame = 1;
}

BucketContainer::BucketContainer() : mCurrentTile({0.0f, 0.0f}) {}

void BucketContainer::updateLocation(float lat, float lon)
{
    struct Candidate
    {
        float lat;
        float lon;
        double dist;
    };
    std::vector<Candidate> missing;
    const double addRadiusM = static_cast<double>(cTileAddingRangeDeg) * 0.5 * 111319.9;
    for (float x = (lat - cTileAddingRangeDeg / 2); x < (lat + cTileAddingRangeDeg / 2); x += cTileSize)
    {
        for (float y = (lon - cTileAddingRangeDeg / 2); y < (lon + cTileAddingRangeDeg / 2); y += cTileSize)
        {
            const double dist = GeoCoordUtils::calculateDistance(lat, lon, x, y);
            if (dist > addRadiusM)
            {
                continue;
            }
            if (!hasTile(x, y))
            {
                missing.push_back({x, y, dist});
            }
        }
    }
    std::sort(missing.begin(), missing.end(),
              [](const Candidate &a, const Candidate &b) { return a.dist < b.dist; });
    const int add = std::min(kAddBucketsPerFrame, static_cast<int>(missing.size()));
    for (int i = 0; i < add; ++i)
    {
        auto tile = std::make_unique<Bucket>(missing[i].lat, missing[i].lon);
        mIndices.insert(tile->index());
        mMap.push_back(std::move(tile));
    }
    mCurrentTile.latitude = lat;
    mCurrentTile.longitude = lon;

    auto iter = mMap.begin();
    while (iter != mMap.end())
    {
        if ((*iter)->distanceTo(lat, lon) > kTileDistanceLimit)
        {
            std::cout << "Remove tile" << std::endl;
            mIndices.erase((*iter)->index());
            iter = mMap.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
    mAirports.update(lat, lon);
}

void BucketContainer::render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up)
{
    int uploaded = 0;
    mDrawn = 0;
    for (auto &tile : mMap)
    {
        tile->pumpLoad();
        if (uploaded < kUploadBucketsPerFrame && tile->uploadIfReady())
        {
            ++uploaded;
        }
        if (!tile->isVisible(eye, forward))
        {
            continue;
        }
        tile->setCamera(proj, eye, forward, up);
        tile->render();
        ++mDrawn;
    }
    mAirports.render(proj, eye, forward, up);
}

bool BucketContainer::hasTile(float lat, float lon) const
{
    return mIndices.count(Bucket::genIndex(lat, lon)) != 0;
}
