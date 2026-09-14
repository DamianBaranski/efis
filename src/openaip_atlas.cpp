#include "openaip_atlas.h"

#include "openaip_client.h"
#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr unsigned char kHasBase = 1;
constexpr unsigned char kHasOverlay = 2;
}

OpenAipAtlas::OpenAipAtlas(int zoom, int radius)
    : mTiles(2 * radius + 1), mN(static_cast<float>(std::exp2(zoom))), mZoom(zoom), mRadius(radius),
      mSlot(static_cast<size_t>(mTiles) * static_cast<size_t>(mTiles), 0)
{
}

OpenAipAtlas::~OpenAipAtlas()
{
    if (mTexture != 0)
    {
        glDeleteTextures(1, &mTexture);
    }
    if (mSurface != nullptr)
    {
        SDL_FreeSurface(mSurface);
    }
}

void OpenAipAtlas::ensureTexture()
{
    if (mTexture != 0)
    {
        return;
    }
    const int width = mTiles * kTilePx;
    const int height = mTiles * kTilePx;
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    std::vector<unsigned char> empty(static_cast<size_t>(width) * static_cast<size_t>(height) * 4, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, empty.data());
    glGenerateMipmap(GL_TEXTURE_2D);
}

void OpenAipAtlas::ensureSurface()
{
    if (mSurface != nullptr)
    {
        return;
    }
    const int width = mTiles * kTilePx;
    const int height = mTiles * kTilePx;
    mSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (mSurface == nullptr)
    {
        std::cerr << "OpenAIP atlas: SDL_CreateRGBSurfaceWithFormat failed" << std::endl;
        return;
    }
    SDL_FillRect(mSurface, nullptr, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
}

void OpenAipAtlas::shiftOrigin(int originX, int originY)
{
    const int dx = originX - mOriginX;
    const int dy = originY - mOriginY;
    mOriginX = originX;
    mOriginY = originY;
    if (mSurface == nullptr || (dx == 0 && dy == 0))
    {
        return;
    }
    if (std::abs(dx) >= mTiles || std::abs(dy) >= mTiles)
    {
        SDL_FillRect(mSurface, nullptr, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
        std::fill(mSlot.begin(), mSlot.end(), 0);
        return;
    }

    SDL_Surface *tmp = SDL_CreateRGBSurfaceWithFormat(0, mSurface->w, mSurface->h, 32, SDL_PIXELFORMAT_RGBA32);
    if (tmp == nullptr)
    {
        SDL_FillRect(mSurface, nullptr, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
        std::fill(mSlot.begin(), mSlot.end(), 0);
        return;
    }
    SDL_FillRect(tmp, nullptr, SDL_MapRGBA(tmp->format, 0, 0, 0, 0));
    SDL_Rect src;
    src.x = std::max(dx, 0) * kTilePx;
    src.y = std::max(dy, 0) * kTilePx;
    src.w = (mTiles - std::abs(dx)) * kTilePx;
    src.h = (mTiles - std::abs(dy)) * kTilePx;
    SDL_Rect dst;
    dst.x = std::max(-dx, 0) * kTilePx;
    dst.y = std::max(-dy, 0) * kTilePx;
    dst.w = src.w;
    dst.h = src.h;
    SDL_SetSurfaceBlendMode(mSurface, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(mSurface, &src, tmp, &dst);
    SDL_SetSurfaceBlendMode(tmp, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(tmp, nullptr, mSurface, nullptr);
    SDL_FreeSurface(tmp);

    std::vector<unsigned char> next(mSlot.size(), 0);
    for (int y = 0; y < mTiles; ++y)
    {
        for (int x = 0; x < mTiles; ++x)
        {
            const int ox = x + dx;
            const int oy = y + dy;
            if (ox >= 0 && ox < mTiles && oy >= 0 && oy < mTiles)
            {
                next[static_cast<size_t>(y * mTiles + x)] = mSlot[static_cast<size_t>(oy * mTiles + ox)];
            }
        }
    }
    mSlot.swap(next);
}

bool OpenAipAtlas::blitSlot(int dx, int dy)
{
    auto &client = OpenAipClient::instance();
    const int tileX = mOriginX + dx;
    const int tileY = mOriginY + dy;
    unsigned char wanted = 0;
    if (client.isCached(mZoom, tileX, tileY, client.basemapLayer()))
    {
        wanted |= kHasBase;
    }
    if (client.isCached(mZoom, tileX, tileY, "openaip"))
    {
        wanted |= kHasOverlay;
    }
    unsigned char &have = mSlot[static_cast<size_t>(dy * mTiles + dx)];
    if (wanted == have)
    {
        return false;
    }

    auto blitLayer = [&](const std::string &layer, SDL_BlendMode blend) {
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
        SDL_BlitSurface(rgba, nullptr, mSurface, &dest);
        SDL_FreeSurface(rgba);
        return true;
    };

    const unsigned char missing = static_cast<unsigned char>(wanted & ~have);
    bool changed = false;
    if ((missing & kHasBase) != 0)
    {
        if (blitLayer(client.basemapLayer(), SDL_BLENDMODE_NONE))
        {
            have |= kHasBase;
            changed = true;
        }
    }
    if ((missing & kHasOverlay) != 0)
    {
        if (blitLayer("openaip", SDL_BLENDMODE_BLEND))
        {
            have |= kHasOverlay;
            changed = true;
        }
    }
    return changed;
}

void OpenAipAtlas::upload()
{
    if (mTexture == 0 || mSurface == nullptr)
    {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int rowPixels = mSurface->pitch / 4;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowPixels);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mSurface->w, mSurface->h, GL_RGBA, GL_UNSIGNED_BYTE, mSurface->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glGenerateMipmap(GL_TEXTURE_2D);
}

void OpenAipAtlas::update(float latitude, float longitude)
{
    auto &client = OpenAipClient::instance();
    mN = static_cast<float>(std::exp2(mZoom));
    client.fetchAround(latitude, longitude, mZoom, mRadius);
    ensureSurface();
    ensureTexture();
    if (mSurface == nullptr)
    {
        return;
    }

    const std::string layer = client.basemapLayer();
    const auto center = OpenAipClient::latLonToTile(latitude, longitude, mZoom);
    const int originX = center.first - mRadius;
    const int originY = center.second - mRadius;
    bool dirty = false;
    if (originX != mOriginX || originY != mOriginY || layer != mBasemapLayer)
    {
        if (layer != mBasemapLayer)
        {
            SDL_FillRect(mSurface, nullptr, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
            std::fill(mSlot.begin(), mSlot.end(), 0);
            mOriginX = originX;
            mOriginY = originY;
        }
        else
        {
            shiftOrigin(originX, originY);
        }
        mBasemapLayer = layer;
        dirty = true;
    }

    int flags = IMG_INIT_PNG;
    if ((IMG_Init(flags) & flags) == 0)
    {
        return;
    }

    for (int dy = 0; dy < mTiles; ++dy)
    {
        for (int dx = 0; dx < mTiles; ++dx)
        {
            if (blitSlot(dx, dy))
            {
                dirty = true;
            }
        }
    }
    if (dirty)
    {
        upload();
    }
}
