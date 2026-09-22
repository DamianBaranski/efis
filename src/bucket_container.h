/// @file bucket_container.h
/// @brief Definition of the BucketContainer class.

#ifndef BUCKET_CONTAINER_H
#define BUCKET_CONTAINER_H

#include "bucket.h"
#include "airport_scenery.h"
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/// @brief Represents a container for managing Buckets.
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

private:
    bool hasTile(float lat, float lon) const;

    /// @brief Represents the current location data.
    struct LocationData
    {
        float latitude;  ///< Latitude of the current location.
        float longitude; ///< Longitude of the current location.
    };

    std::vector<std::unique_ptr<Bucket>> mMap;    ///< Vector containing pointers to Buckets.
    AirportScenery mAirports;
    LocationData mCurrentTile;                    ///< Current location data.
    const double kTileDistanceLimit = 500 * 1000; ///< Distance limit for tiles (500 km).
    const float cTileSize = 0.1f;                 ///< Size of each tile in degrees.
    /// Full width of loaded scenery. 2° = ±1°, matching download-fg-terrain.sh --radius-deg 1.
    const float cTileAddingRangeDeg = 2.0f;
};

#endif // BUCKET_CONTAINER_H
