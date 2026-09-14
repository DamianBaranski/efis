#ifndef OPENAIP_ATLAS_H
#define OPENAIP_ATLAS_H

#include <GLES3/gl3.h>
#include <string>
#include <vector>

struct SDL_Surface;

/// Stitches cached OpenAIP PNG tiles into one texture for draping on terrain.
class OpenAipAtlas
{
public:
    /// High-res clip around the aircraft (~1.5 m/pixel, a few km).
    static constexpr int kDetailZoom = 16;
    static constexpr int kDetailRadius = 7;
    /// Wide clip matching the FlightGear ±1° scenery window.
    static constexpr int kWideZoom = 13;
    static constexpr int kWideRadius = 15;
    static constexpr int kTilePx = 256;
    static constexpr int kFeatherPx = 2;
    static constexpr int kMinParentZoom = 10;

    OpenAipAtlas(int zoom, int radius);
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
    int zoom() const { return mZoom; }
    int radius() const { return mRadius; }

private:
    void ensureTexture();
    void ensureSurface();
    void shiftOrigin(int originX, int originY);
    bool blitSlot(int dx, int dy);
    void featherSeams(SDL_Surface *dest) const;
    void upload();
    void uploadMipmaps(SDL_Surface *src);

    GLuint mTexture = 0;
    SDL_Surface *mSurface = nullptr;
    int mOriginX = -100000;
    int mOriginY = -100000;
    int mTiles = 1;
    float mN = 1.0f;
    int mZoom = 0;
    int mRadius = 0;
    std::string mBasemapLayer;
    std::vector<unsigned char> mSlot;
};

#endif
