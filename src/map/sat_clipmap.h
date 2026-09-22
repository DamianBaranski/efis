/// \file sat_clipmap.h
/// Three rings of Mercator imagery around the camera.
#ifndef SAT_CLIPMAP_H
#define SAT_CLIPMAP_H

#include <GLES3/gl3.h>
#include <cstddef>
#include <cstdint>

/// Mercator imagery clipmap. Each ring is a square texture array, but only
/// the inscribed disk around the camera is fetched and sampled.
class SatClipmap
{
public:
    /// Default tiles on one side of a ring.
    static constexpr int kGrid = 8;
    /// Largest ring the mask arrays can hold.
    static constexpr int kMaxGrid = 16;
    /// Words in a presence mask for kMaxGrid.
    static constexpr int kMaskWords = (kMaxGrid * kMaxGrid) / 32;
    /// Pixels on one side of a source tile.
    static constexpr int kTilePx = 256;
    /// Zoom of the outermost ring.
    static constexpr int kWideZoom = 11;
    /// Zoom of the middle ring.
    static constexpr int kMidZoom = 13;
    /// Zoom of the close-in ring.
    static constexpr int kFineZoom = 16;

    SatClipmap() = default;
    /// Releases the three ring textures.
    ~SatClipmap();

    SatClipmap(const SatClipmap &) = delete;
    SatClipmap &operator=(const SatClipmap &) = delete;

    /// How much of one ring is filled.
    struct Progress
    {
        int done = 0;        ///< Tiles uploaded.
        int total = 0;       ///< Tiles in the ring.
        size_t cpuBytes = 0; ///< Decoded pixels still on the CPU.
        size_t gpuBytes = 0; ///< Bytes in the ring texture.
        bool ready = false;  ///< True when done equals total.
    };

    /// Samples the OpenAIP chart in addition to the satellite base map.
    void setChartOverlay(bool enable);
    /// Close-in imagery zoom, from 12 through 18.
    void setDetailZoom(int zoom);
    /// Tile count on one side of the close-in ring.
    void setFineGrid(int grid);
    /// Mid-ring zoom.
    void setMidZoom(int zoom);
    /// Tile count on one side of the mid ring.
    void setMidGrid(int grid);
    /// Close-in zoom currently in use.
    int detailZoom() const { return mDetailZoom; }
    /// Tiles on one side of the close-in ring.
    int fineGrid() const;
    /// Tiles on one side of the mid ring.
    int midGrid() const;
    /// Mid-ring zoom currently in use.
    int midZoom() const;
    /// Default ring width for a zoom. Higher zooms use a smaller grid.
    static int gridForZoom(int zoom);
    /// Uploads up to maxUploads tiles around the camera.
    /// \param latitude Degrees.
    /// \param longitude Degrees.
    void pump(float latitude, float longitude, int maxUploads);

    /// True when every slot in the active rings has a tile.
    bool ready() const;
    /// Tiles finished in the close-in ring.
    Progress fineProgress() const;
    /// Tiles finished in the mid and wide rings.
    Progress coarseProgress() const;
    /// GPU bytes held by the three rings.
    size_t gpuBytes() const;

    /// Texture ids and tile origins the terrain shader samples this frame.
    struct View
    {
        bool active = false;
        GLuint fineTex = 0;
        GLuint midTex = 0;
        GLuint wideTex = 0;
        int fineOriginX = 0;
        int fineOriginY = 0;
        int midOriginX = 0;
        int midOriginY = 0;
        int wideOriginX = 0;
        int wideOriginY = 0;
        int fineZoom = kFineZoom;
        int midZoom = kMidZoom;
        int wideZoom = kWideZoom;
        int fineGrid = kGrid;
        int midGrid = kGrid;
        uint32_t fineMask[kMaskWords]{};
        uint32_t midMask[kMaskWords]{};
        uint32_t wideMask0 = 0;
        uint32_t wideMask1 = 0;
    };

    /// Texture ids and origins the terrain shader samples this frame.
    View view() const;

private:
    struct Ring
    {
        int zoom = 0;
        GLuint texture = 0;
        int grid = kGrid;
        int originX = 0;
        int originY = 0;
        int slotX[kMaxGrid * kMaxGrid]{};
        int slotY[kMaxGrid * kMaxGrid]{};
        bool created = false;
    };

    void ensureRing(Ring &ring);
    void clearSlots(Ring &ring);
    void invalidate();
    bool holds(const Ring &ring, int tileX, int tileY) const;
    bool uploadTile(Ring &ring, int tileX, int tileY);
    void maskFor(const Ring &ring, uint32_t *words, int wordCount) const;
    Progress progressFor(const Ring &ring) const;
    int layerOf(const Ring &ring, int tileX, int tileY) const;
    static int windowOrigin(int cameraTile, int grid);
    void resizeRing(Ring &ring, int grid);

    Ring mWide{};
    Ring mMid{};
    Ring mFine{};
    int mDetailZoom = kFineZoom;
    bool mChartOverlay = false;
    bool mLogged = false;
};

#endif
