/// \file openaip_atlas.h
/// One stitched texture built from cached map tiles.
#ifndef OPENAIP_ATLAS_H
#define OPENAIP_ATLAS_H

#include <GLES3/gl3.h>
#include <cstddef>
#include <string>
#include <vector>

struct SDL_Surface;

/// Stitches cached OpenAIP PNG tiles into one texture for draping on terrain.
class OpenAipAtlas
{
public:
    /// High-res clip around the aircraft, about 1.5 m per pixel.
    static constexpr int kDetailZoom = 16;
    /// Tiles from the aircraft out to the edge of the high-res clip.
    static constexpr int kDetailRadius = 7;
    /// Wide clip matching the FlightGear ±1° scenery window.
    static constexpr int kWideZoom = 13;
    /// Tiles from the aircraft out to the edge of the wide clip.
    static constexpr int kWideRadius = 15;
    /// Pixels on one side of a source tile.
    static constexpr int kTilePx = 256;
    /// Pixels blended across a tile seam.
    static constexpr int kFeatherPx = 2;
    /// Coarsest zoom used to fill a hole in a missing tile.
    static constexpr int kMinParentZoom = 10;

    /// Allocates the stitch for one zoom and radius. Tiles are not loaded yet.
    /// \param zoom XYZ zoom.
    /// \param radius Tiles from the center to the edge.
    OpenAipAtlas(int zoom, int radius);
    /// Releases the GPU texture.
    ~OpenAipAtlas();

    OpenAipAtlas(const OpenAipAtlas &) = delete;
    OpenAipAtlas &operator=(const OpenAipAtlas &) = delete;

    /// How much of the stitch is filled.
    struct Progress
    {
        int done = 0;           ///< Tiles uploaded.
        int total = 0;          ///< Tiles in the stitch.
        size_t cpuBytes = 0;    ///< Decoded pixels still on the CPU.
        size_t gpuBytes = 0;    ///< Bytes in the texture.
        bool ready = false;     ///< True when done equals total.
    };

    /// Decoded CPU bytes for a square of radius tiles.
    static size_t cpuBytesFor(int radius);
    /// GPU bytes for a square of radius tiles.
    static size_t gpuBytesFor(int radius);

    /// Chooses the satellite base map, the OpenAIP chart, or both.
    void setLayers(bool basemap, bool overlay);
    /// Recenters the stitch on the aircraft. Does not upload.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    void update(float latitude, float longitude);
    /// Uploads up to maxBlits tiles into the stitch.
    void pump(float latitude, float longitude, int maxBlits);
    /// Tiles finished against tiles required.
    Progress progress() const;
    /// True when every slot in the stitch has a tile.
    bool ready() const { return mReady; }

    /// GPU texture of the stitch. 0 before the first upload.
    GLuint texture() const { return mTexture; }
    /// XYZ column of the stitch origin.
    float originX() const { return static_cast<float>(mOriginX); }
    /// XYZ row of the stitch origin.
    float originY() const { return static_cast<float>(mOriginY); }
    /// Tiles across the stitch.
    float tilesX() const { return static_cast<float>(mTiles); }
    /// Tiles down the stitch.
    float tilesY() const { return static_cast<float>(mTiles); }
    /// World size of one tile at this zoom, in the shader's units.
    float n() const { return mN; }
    /// XYZ zoom of this stitch.
    int zoom() const { return mZoom; }
    /// Tiles from the center to the edge.
    int radius() const { return mRadius; }

private:
    void ensureTexture();
    void ensureSurface();
    void releaseCpuStore();
    void shiftOrigin(int originX, int originY);
    bool shiftGpuTiles(int dx, int dy);
    bool blitSlot(int dx, int dy);
    void featherSeams(SDL_Surface *dest) const;
    void upload();
    void uploadTile(int dx, int dy);
    void uploadTileFrom(SDL_Surface *tile, int dx, int dy);
    void uploadMipmaps(SDL_Surface *src);
    int countPending() const;

    GLuint mTexture = 0;
    GLuint mRowTex = 0;
    GLuint mShiftFbo = 0;
    SDL_Surface *mSurface = nullptr;
    int mOriginX = -100000;
    int mOriginY = -100000;
    int mTiles = 1;
    float mN = 1.0f;
    int mZoom = 0;
    int mRadius = 0;
    std::string mBasemapLayer;
    bool mWantBasemap = true;
    bool mWantOverlay = false;
    bool mReady = false;
    bool mNeedMips = false;
    bool mReleasedCpu = false;
    bool mUseMips = false;
    int mScan = 0;
    std::vector<unsigned char> mSlot;
};

#endif
