/// @file Bucket.h
/// @brief Definition of the Bucket class.

#ifndef BUCKET_H
#define BUCKET_H

#include "btg_file.h"
#include "shader.h"
#include <thread>
#include <atomic>
#include <glm/glm.hpp>

/// @brief Represents a geographic bucket for rendering terrain data.
class Bucket
{
public:
    /// @brief Constructor for Bucket class.
    /// @param lat Latitude of the bucket.
    /// @param lon Longitude of the bucket.
    Bucket(float lat, float lon);

    /// @brief Destructor for Bucket class.
    ~Bucket();

    /// @brief Renders the bucket.
    void render();

    /// @brief Checks if a geographic point is contained within the bucket.
    /// @param lat Latitude of the point.
    /// @param lon Longitude of the point.
    /// @return True if the point is contained within the bucket, false otherwise.
    bool contain(float lat, float lon);

    /// @brief Calculates the distance from a geographic point to the bucket.
    /// @param lat Latitude of the point.
    /// @param lon Longitude of the point.
    /// @return Distance from the point to the bucket in meters.
    double distanceTo(float lat, float lon) const;

    /// @brief Builds a tile-local view from an ECEF camera and uploads the MVP matrix.
    void setCamera(const glm::mat4 &proj, const glm::dvec3 &eye, const glm::vec3 &forward, const glm::vec3 &up);

private:
    /// @brief Loads terrain data from file.
    /// @param filename The filename of the terrain data file.
    void loadFile(const std::string &filename);

    /// @brief Generates the path for the tile based on latitude and longitude.
    /// @return The generated tile path.
    std::string generateTilePath();

    /// @brief Generates the index for the bucket based on latitude and longitude.
    /// @param lat Latitude of the bucket.
    /// @param lon Longitude of the bucket.
    /// @return The generated index.
    long int genIndex(float lat, float lon);

    /// @brief Gets the span for a given latitude.
    /// @param l Latitude.
    /// @return The span value.
    static double getSpan(double l);

    float mLon;                   ///< Longitude of the bucket.
    float mLat;                   ///< Latitude of the bucket.
    glm::mat4 mModelMat{1.0f};
    glm::dvec3 mCenter{0.0};
    Shader mShader;               ///< Shader for rendering the bucket.
    long int mIndex;              ///< Index of the bucket.
    std::thread mLoadingThread;   ///< Thread for loading terrain data.
    std::atomic<uint8_t> mLoaded; ///< Atomic flag indicating whether terrain data is loaded.
    std::vector<Triangles> mMesh; ///< Mesh representing the terrain geometry.

    static constexpr char const cTilePath[] = "../resources/terrain/"; ///< Path to the terrain tiles.
    static constexpr char const cTileFileExt[] = ".btg.gz";            ///< File extension for terrain tiles.
};

#endif // BUCKET_H
