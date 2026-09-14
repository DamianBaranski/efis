#include "openaip_atlas.h"

#include "openaip_client.h"
#include <SDL.h>
#include <SDL_image.h>
#include <cmath>
#include <iostream>
#include <string>

OpenAipAtlas::OpenAipAtlas()
{
    mN = static_cast<float>(std::exp2(kZoom));
}

int OpenAipAtlas::zoomForStyle(const std::string &style)
{
    return (style == "satellite") ? kSatelliteZoom : kZoom;
}

OpenAipAtlas::~OpenAipAtlas()
{
    if (mTexture != 0)
    {
        glDeleteTextures(1, &mTexture);
    }
}

void OpenAipAtlas::ensureTexture()
{
    if (mTexture != 0)
    {
        return;
    }
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const unsigned char pixel[4] = {0, 0, 0, 0};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
}

int OpenAipAtlas::countCached() const
{
    auto &client = OpenAipClient::instance();
    int count = 0;
    for (int dy = 0; dy < mTiles; ++dy)
    {
        for (int dx = 0; dx < mTiles; ++dx)
        {
            const int tileX = mOriginX + dx;
            const int tileY = mOriginY + dy;
            if (client.isCached(mZoom, tileX, tileY, client.basemapLayer()) ||
                client.isCached(mZoom, tileX, tileY, "openaip"))
            {
                ++count;
            }
        }
    }
    return count;
}

void OpenAipAtlas::update(float latitude, float longitude)
{
    auto &client = OpenAipClient::instance();
    const int zoom = zoomForStyle(client.basemapStyle());
    mN = static_cast<float>(std::exp2(zoom));
    client.fetchAround(latitude, longitude, zoom, kRadius);

    const std::string layer = client.basemapLayer();
    const auto center = OpenAipClient::latLonToTile(latitude, longitude, zoom);
    const int originX = center.first - kRadius;
    const int originY = center.second - kRadius;
    const bool originChanged =
        (originX != mOriginX || originY != mOriginY || layer != mBasemapLayer || zoom != mZoom);
    if (originChanged)
    {
        mOriginX = originX;
        mOriginY = originY;
        mBasemapLayer = layer;
        mZoom = zoom;
        mCachedCount = -1;
        mFramesUntilRetry = 0;
    }

    if (mFramesUntilRetry > 0)
    {
        --mFramesUntilRetry;
        if (!originChanged)
        {
            return;
        }
    }

    const int cached = countCached();
    if (!originChanged && cached == mCachedCount && mTexture != 0)
    {
        mFramesUntilRetry = 20;
        return;
    }

    mCachedCount = cached;
    rebuild();
    mFramesUntilRetry = (cached == mTiles * mTiles) ? 60 : 15;
}

void OpenAipAtlas::rebuild()
{
    ensureTexture();

    int flags = IMG_INIT_PNG;
    if ((IMG_Init(flags) & flags) == 0)
    {
        std::cerr << "OpenAIP atlas: PNG loader missing" << std::endl;
        return;
    }

    const int width = mTiles * kTilePx;
    const int height = mTiles * kTilePx;
    SDL_Surface *atlas = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!atlas)
    {
        std::cerr << "OpenAIP atlas: SDL_CreateRGBSurfaceWithFormat failed" << std::endl;
        return;
    }
    SDL_FillRect(atlas, nullptr, SDL_MapRGBA(atlas->format, 230, 230, 220, 255));

    auto &client = OpenAipClient::instance();
    auto blitLayer = [&](int tileX, int tileY, int dx, int dy, const std::string &layer, SDL_BlendMode blend) {
        if (!client.isCached(mZoom, tileX, tileY, layer))
        {
            return false;
        }
        SDL_Surface *tile = IMG_Load(client.cachePath(mZoom, tileX, tileY, layer).c_str());
        if (!tile)
        {
            return false;
        }
        SDL_Surface *rgba = SDL_ConvertSurfaceFormat(tile, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(tile);
        if (!rgba)
        {
            return false;
        }
        SDL_Rect dest{dx * kTilePx, dy * kTilePx, kTilePx, kTilePx};
        SDL_SetSurfaceBlendMode(rgba, blend);
        SDL_BlitSurface(rgba, nullptr, atlas, &dest);
        SDL_FreeSurface(rgba);
        return true;
    };

    int blitted = 0;
    for (int dy = 0; dy < mTiles; ++dy)
    {
        for (int dx = 0; dx < mTiles; ++dx)
        {
            const int tileX = mOriginX + dx;
            const int tileY = mOriginY + dy;
            const bool base = blitLayer(tileX, tileY, dx, dy, client.basemapLayer(), SDL_BLENDMODE_NONE);
            const bool aip = blitLayer(tileX, tileY, dx, dy, "openaip", SDL_BLENDMODE_BLEND);
            if (base || aip)
            {
                ++blitted;
            }
        }
    }

    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas->pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);
    SDL_FreeSurface(atlas);

    std::cout << "OpenAIP atlas " << mTiles << "x" << mTiles << " at "
              << mZoom << "/" << mOriginX << "/" << mOriginY
              << " tiles " << blitted << "/" << (mTiles * mTiles) << std::endl;
}
