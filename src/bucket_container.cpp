#include "bucket_container.h"
#include <iostream>

BucketContainer::BucketContainer() : mCurrentTile({0.0f, 0.0f}) {}

void BucketContainer::updateLocation(float lat, float lon)
{
    if (std::abs(mCurrentTile.latitude - lat) < cTileSize && std::abs(mCurrentTile.longitude - lon) < cTileSize)
    {
        return;
    }

    for (float x = (lat - cTileAddingRangeDeg / 2); x < (lat + cTileAddingRangeDeg / 2); x += cTileSize)
    {
        for (float y = (lon - cTileAddingRangeDeg / 2); y < (lon + cTileAddingRangeDeg / 2); y += cTileSize)
        {
            if (checkTile(x, y))
            {
                mCurrentTile.latitude = lat;
                mCurrentTile.longitude = lon;
            }
        }
    }

    // Clear out-of-range tiles
    auto iter = mMap.begin();
    while (iter != mMap.end())
    {
        if ((*iter)->distanceTo(lat, lon) > kTileDistanceLimit)
        {
            std::cout << "Remove tile" << std::endl;
            iter = mMap.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

void BucketContainer::render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up)
{
    for (auto &tile : mMap)
    {
        tile->setCamera(proj, eye, forward, up);
        tile->render();
    }
}

bool BucketContainer::checkTile(float lat, float lon)
{
    for (auto &tile : mMap)
    {
        if (tile->contain(lat, lon))
        {
            return false;
        }
    }
    mMap.push_back(std::make_unique<Bucket>(lat, lon));
    return true;
}
