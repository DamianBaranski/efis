/// @file bucket_container.h
/// @brief Definition of the BucketContainer class.

#ifndef BUCKET_CONTAINER_H
#define BUCKET_CONTAINER_H

#include "bucket.h"
#include <vector>
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

    /// @brief Renders all Buckets in the container.
    /// @param mvpMat The Model-View-Projection (MVP) matrix.
    void render(glm::mat4 mvpMat);

private:
    /// @brief Checks if a Bucket exists for the given location, creates one if not.
    /// @param lat Latitude of the location.
    /// @param lon Longitude of the location.
    /// @return True if a Bucket was created, false otherwise.
    bool checkTile(float lat, float lon);

    /// @brief Represents the current location data.
    struct LocationData
    {
        float latitude;  ///< Latitude of the current location.
        float longitude; ///< Longitude of the current location.
    };

    std::vector<std::unique_ptr<Bucket>> mMap;    ///< Vector containing pointers to Buckets.
    LocationData mCurrentTile;                    ///< Current location data.
    const double kTileDistanceLimit = 500 * 1000; ///< Distance limit for tiles (500 km).
    const float cTileSize = 0.1f;                 ///< Size of each tile in degrees.
    const float cTileAddingRangeDeg = 1.0f;
};

#endif // BUCKET_CONTAINER_H
