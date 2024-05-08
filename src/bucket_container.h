#include "bucket.h"
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class BucketContainer
{
public:
    BucketContainer()
    {
    }

    void updateLocation(float lat, float lon)
    {
        if(abs(mCurrentTile.latitude-lat)<0.1 && abs(mCurrentTile.longitude-lon)<0.1) {
            return;
        } 

        for (float x = (lat - 0.2); x < (lat + 0.2); x+=0.1)
        {
            for (float y = (lon - 0.2); y < (lon + 0.2); y+=0.1)
            {
                if(checkTile(x, y)) {
                    mCurrentTile.latitude = lat;
                    mCurrentTile.longitude = lon;
                }
            }
        }

        // Clear out-of-range tiles
        auto iter = mMap.begin();
        while (iter != mMap.end())
        {
            if ((*iter)->distanceTo(lat, lon) > 100 * 1000) // 100km
            {
                std::cout << "Remove tile:" << std::endl;
                iter = mMap.erase(iter);
            }
            else
            {
                ++iter;
            }
        }
    }

    void render(glm::mat4 mvpMat)
    {
        for (auto &tile : mMap)
        {
            tile->setMvpMatrix(mvpMat);
            tile->render();
        }
    }

private:
    bool checkTile(float lat, float lon)
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

    std::vector<std::unique_ptr<Bucket>> mMap;
    LocationData mCurrentTile;
};