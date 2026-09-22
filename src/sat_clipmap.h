#ifndef SAT_CLIPMAP_H
#define SAT_CLIPMAP_H

#include <GLES3/gl3.h>
#include <cstddef>
#include <cstdint>

/// Mercator imagery clipmap. The far ring is a fixed 8x8 of zoom 13.
/// The close ring allocates more 256 px tiles as zoom rises, so texture
/// memory grows with the selector. Scenery tiles are not involved.
class SatClipmap
{
public:
    static constexpr int kGrid = 8;
    static constexpr int kMaxGrid = 16;
    static constexpr int kMaskWords = (kMaxGrid * kMaxGrid) / 32;
    static constexpr int kTilePx = 256;
    static constexpr int kWideZoom = 11;
    static constexpr int kMidZoom = 13;
    static constexpr int kFineZoom = 16;

    SatClipmap() = default;
    ~SatClipmap();

    SatClipmap(const SatClipmap &) = delete;
    SatClipmap &operator=(const SatClipmap &) = delete;

    struct Progress
    {
        int done = 0;
        int total = 0;
        size_t cpuBytes = 0;
        size_t gpuBytes = 0;
        bool ready = false;
    };

    void setChartOverlay(bool enable);
    /// Close-in imagery zoom, from 12 through 18.
    void setDetailZoom(int zoom);
    void setFineGrid(int grid);
    void setMidZoom(int zoom);
    void setMidGrid(int grid);
    int detailZoom() const { return mDetailZoom; }
    int fineGrid() const;
    int midGrid() const;
    int midZoom() const;
    static int gridForZoom(int zoom);
    void pump(float latitude, float longitude, int maxUploads);

    bool ready() const;
    Progress fineProgress() const;
    Progress coarseProgress() const;
    size_t gpuBytes() const;

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
