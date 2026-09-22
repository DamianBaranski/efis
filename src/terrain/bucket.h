/// \file bucket.h
/// One FlightGear terrain tile, from disk to the GPU.

#ifndef BUCKET_H
#define BUCKET_H

#include "btg_file.h"
#include "shader.h"
#include <thread>
#include <atomic>
#include <glm/glm.hpp>

/// One 0.1 degree terrain tile. Loading happens off the render thread.
class Bucket
{
public:
    /// Names the tile. The file is not read until pumpLoad().
    /// \param lat Degrees, north positive.
    /// \param lon Degrees, east positive.
    Bucket(float lat, float lon);

    /// @brief Destructor for Bucket class.
    ~Bucket();

    /// Starts the file thread when a load slot is free.
    void pumpLoad();

    /// Uploads CPU mesh to the GPU at most once. Returns true if this call uploaded.
    bool uploadIfReady();

    /// @brief Renders the bucket.
    void render();

    /// True after the mesh has been uploaded.
    bool gpuReady() const { return mState.load(std::memory_order_acquire) == 3; }
    /// Stable id from genIndex for this tile's latitude and longitude.
    long index() const { return mIndex; }
    /// True when the tile center is in front of the camera.
    bool isVisible(const glm::dvec3 &eye, const glm::vec3 &forward) const;
    /// Id for the 0.1 degree tile that contains the point.
    static long int genIndex(float lat, float lon);

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
    void tileBounds(double &lat0, double &lat1, double &lon0, double &lon1) const;
    void appendUnderlay();

    /// @brief Generates the path for the tile based on latitude and longitude.
    /// @return The generated tile path.
    std::string generateTilePath();

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
    std::string mFilename;
    std::thread mLoadingThread;   ///< Thread for loading terrain data.
    std::atomic<uint8_t> mState{0}; ///< 0 idle, 1 loading, 2 cpu ready, 3 gpu ready, 4 missing.
    std::vector<Triangles> mMesh; ///< Mesh representing the terrain geometry.

    static constexpr char const cTileFileExt[] = ".btg.gz"; ///< File extension for terrain tiles.
};

#endif // BUCKET_H
