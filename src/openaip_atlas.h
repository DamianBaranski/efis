#ifndef OPENAIP_ATLAS_H
#define OPENAIP_ATLAS_H

#include <GLES3/gl3.h>
#include <string>

/// Stitches cached OpenAIP PNG tiles into one texture for draping on terrain.
class OpenAipAtlas
{
public:
    static constexpr int kZoom = 13;
    static constexpr int kSatelliteZoom = 16;
    static constexpr int kRadius = 7;
    static constexpr int kTilePx = 256;

    static int zoomForStyle(const std::string &style);

    OpenAipAtlas();
    ~OpenAipAtlas();

    OpenAipAtlas(const OpenAipAtlas &) = delete;
    OpenAipAtlas &operator=(const OpenAipAtlas &) = delete;

    void update(float latitude, float longitude);

    GLuint texture() const { return mTexture; }
    float originX() const { return static_cast<float>(mOriginX); }
    float originY() const { return static_cast<float>(mOriginY); }
    float tilesX() const { return static_cast<float>(mTiles); }
    float tilesY() const { return static_cast<float>(mTiles); }
    float n() const { return mN; }

private:
    void ensureTexture();
    void rebuild();
    int countCached() const;

    GLuint mTexture = 0;
    int mOriginX = -100000;
    int mOriginY = -100000;
    int mTiles = 2 * kRadius + 1;
    float mN = 8192.0f;
    int mZoom = kZoom;
    int mCachedCount = -1;
    int mFramesUntilRetry = 0;
    std::string mBasemapLayer;
};

#endif
