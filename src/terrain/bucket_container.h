/// \file bucket_container.h
/// Set of terrain tiles kept around the camera.

#ifndef BUCKET_CONTAINER_H
#define BUCKET_CONTAINER_H

#include "bucket.h"
#include "airport_scenery.h"
#include <vector>
#include <memory>
#include <unordered_set>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/// Loads and draws the tiles inside the camera disk, about one degree across.
class BucketContainer
{
public:
    /// @brief Constructor for BucketContainer class.
    BucketContainer();

    /// @brief Updates the location of the container.
    /// @param lat Latitude of the new location.
    /// @param lon Longitude of the new location.
    void updateLocation(float lat, float lon);

    /// @brief Renders all Buckets in the container using an ECEF camera.
    void render(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

    /// Tiles currently held, including ones still loading.
    int loadedCount() const { return static_cast<int>(mMap.size()); }
    /// Tiles submitted on the last render.
    int drawnCount() const { return mDrawn; }

private:
    bool hasTile(float lat, float lon) const;

    /// @brief Represents the current location data.
    struct LocationData
    {
        float latitude;  ///< Latitude of the current location.
        float longitude; ///< Longitude of the current location.
    };

    std::vector<std::unique_ptr<Bucket>> mMap;    ///< Vector containing pointers to Buckets.
    std::unordered_set<long> mIndices;
    AirportScenery mAirports;
    LocationData mCurrentTile;                    ///< Current location data.
    mutable int mDrawn = 0;
    const double kTileDistanceLimit = 160 * 1000;
    const float cTileSize = 0.1f;
    /// Square search window width in degrees; only the inscribed ~1° disk is added.
    const float cTileAddingRangeDeg = 2.0f;
};

#endif // BUCKET_CONTAINER_H
