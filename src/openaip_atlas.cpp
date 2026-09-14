#include "openaip_atlas.h"

#include "openaip_client.h"
#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace
{
constexpr unsigned char kHasNativeBase = 1;
constexpr unsigned char kHasParentBase = 2;
constexpr unsigned char kHasNativeOverlay = 4;
constexpr unsigned char kHasParentOverlay = 8;
constexpr unsigned char kHasAnyBase = kHasNativeBase | kHasParentBase;

bool parentCached(int zoom, int tileX, int tileY, const std::string &layer)
{
    auto &client = OpenAipClient::instance();
    for (int parentZ = zoom - 1; parentZ >= OpenAipAtlas::kMinParentZoom; --parentZ)
    {
        const int shift = zoom - parentZ;
        if ((OpenAipAtlas::kTilePx >> shift) < 1)
        {
            break;
        }
        if (client.isCached(parentZ, tileX >> shift, tileY >> shift, layer))
        {
            return true;
        }
    }
    return false;
}

unsigned char wantedFlags(int zoom, int tileX, int tileY, const std::string &baseLayer)
{
    auto &client = OpenAipClient::instance();
    unsigned char wanted = 0;
    if (client.isCached(zoom, tileX, tileY, baseLayer))
    {
        wanted |= kHasNativeBase;
    }
    else if (parentCached(zoom, tileX, tileY, baseLayer))
    {
        wanted |= kHasParentBase;
    }
    if (client.isCached(zoom, tileX, tileY, "openaip"))
    {
        wanted |= kHasNativeOverlay;
    }
    else if (parentCached(zoom, tileX, tileY, "openaip"))
    {
        wanted |= kHasParentOverlay;
    }
    return wanted;
}

SDL_Surface *loadRgba(const std::string &path)
{
    SDL_Surface *tile = IMG_Load(path.c_str());
    if (!tile)
    {
        return nullptr;
    }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(tile, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(tile);
    return rgba;
}

SDL_Surface *loadNativeOrParent(int zoom, int tileX, int tileY, const std::string &layer, bool &fromParent)
{
    fromParent = false;
    auto &client = OpenAipClient::instance();
    if (client.isCached(zoom, tileX, tileY, layer))
    {
        return loadRgba(client.cachePath(zoom, tileX, tileY, layer));
    }
    for (int parentZ = zoom - 1; parentZ >= OpenAipAtlas::kMinParentZoom; --parentZ)
    {
        const int shift = zoom - parentZ;
        const int childPx = OpenAipAtlas::kTilePx >> shift;
        if (childPx < 1)
        {
            break;
        }
        const int parentX = tileX >> shift;
        const int parentY = tileY >> shift;
        if (!client.isCached(parentZ, parentX, parentY, layer))
        {
            continue;
        }
        SDL_Surface *parent = loadRgba(client.cachePath(parentZ, parentX, parentY, layer));
        if (!parent)
        {
            continue;
        }
        SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, OpenAipAtlas::kTilePx, OpenAipAtlas::kTilePx, 32,
                                                          SDL_PIXELFORMAT_RGBA32);
        if (!out)
        {
            SDL_FreeSurface(parent);
            return nullptr;
        }
        const int srcX = (tileX - (parentX << shift)) * childPx;
        const int srcY = (tileY - (parentY << shift)) * childPx;
        SDL_Rect src{srcX, srcY, childPx, childPx};
        SDL_Rect dst{0, 0, OpenAipAtlas::kTilePx, OpenAipAtlas::kTilePx};
        SDL_SetSurfaceBlendMode(parent, SDL_BLENDMODE_NONE);
        SDL_BlitScaled(parent, &src, out, &dst);
        SDL_FreeSurface(parent);
        fromParent = true;
        return out;
    }
    return nullptr;
}

unsigned char *pxAt(SDL_Surface *surface, int x, int y)
{
    return static_cast<unsigned char *>(surface->pixels) + y * surface->pitch + x * 4;
}

void mixPx(unsigned char *out, const unsigned char *a, const unsigned char *b, float t)
{
    const float u = 1.0f - t;
    out[0] = static_cast<unsigned char>(a[0] * u + b[0] * t + 0.5f);
    out[1] = static_cast<unsigned char>(a[1] * u + b[1] * t + 0.5f);
    out[2] = static_cast<unsigned char>(a[2] * u + b[2] * t + 0.5f);
    out[3] = static_cast<unsigned char>(a[3] * u + b[3] * t + 0.5f);
}

void featherVertical(SDL_Surface *surface, int seamX, int y0, int y1)
{
    const int feather = OpenAipAtlas::kFeatherPx;
    if (seamX - feather < 0 || seamX + feather > surface->w)
    {
        return;
    }
    const int width = feather * 2;
    std::vector<unsigned char> orig(static_cast<size_t>(width) * 4u);
    for (int y = y0; y < y1; ++y)
    {
        for (int i = 0; i < width; ++i)
        {
            std::memcpy(orig.data() + static_cast<size_t>(i) * 4u, pxAt(surface, seamX - feather + i, y), 4);
        }
        for (int i = 0; i < width; ++i)
        {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(width);
            const unsigned char *left = orig.data() + static_cast<size_t>(std::min(i, feather - 1)) * 4u;
            const unsigned char *right = orig.data() + static_cast<size_t>(feather + std::max(0, i - feather)) * 4u;
            mixPx(pxAt(surface, seamX - feather + i, y), left, right, t);
        }
    }
}

void featherHorizontal(SDL_Surface *surface, int seamY, int x0, int x1)
{
    const int feather = OpenAipAtlas::kFeatherPx;
    if (seamY - feather < 0 || seamY + feather > surface->h)
    {
        return;
    }
    const int width = feather * 2;
    std::vector<unsigned char> orig(static_cast<size_t>(width) * 4u);
    for (int x = x0; x < x1; ++x)
    {
        for (int i = 0; i < width; ++i)
        {
            std::memcpy(orig.data() + static_cast<size_t>(i) * 4u, pxAt(surface, x, seamY - feather + i), 4);
        }
        for (int i = 0; i < width; ++i)
        {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(width);
            const unsigned char *top = orig.data() + static_cast<size_t>(std::min(i, feather - 1)) * 4u;
            const unsigned char *bottom = orig.data() + static_cast<size_t>(feather + std::max(0, i - feather)) * 4u;
            mixPx(pxAt(surface, x, seamY - feather + i), top, bottom, t);
        }
    }
}

void copySurfaceRgba(SDL_Surface *surface, std::vector<unsigned char> &out)
{
    const int width = surface->w;
    const int height = surface->h;
    out.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    const auto *src = static_cast<const unsigned char *>(surface->pixels);
    for (int y = 0; y < height; ++y)
    {
        std::memcpy(out.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4u,
                    src + y * surface->pitch, static_cast<size_t>(width) * 4u);
    }
}

void boxFilter2x2(const unsigned char *a, const unsigned char *b, const unsigned char *c, const unsigned char *d,
                  unsigned char *out)
{
    const int alphaA = a[3];
    const int alphaB = b[3];
    const int alphaC = c[3];
    const int alphaD = d[3];
    const int sumA = alphaA + alphaB + alphaC + alphaD;
    if (sumA <= 0)
    {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    out[0] = static_cast<unsigned char>((a[0] * alphaA + b[0] * alphaB + c[0] * alphaC + d[0] * alphaD) / sumA);
    out[1] = static_cast<unsigned char>((a[1] * alphaA + b[1] * alphaB + c[1] * alphaC + d[1] * alphaD) / sumA);
    out[2] = static_cast<unsigned char>((a[2] * alphaA + b[2] * alphaB + c[2] * alphaC + d[2] * alphaD) / sumA);
    out[3] = static_cast<unsigned char>(sumA / 4);
}

void downsamplePerTile(const std::vector<unsigned char> &src, int srcW, int srcH, int tiles, int tilePx,
                       std::vector<unsigned char> &dst, int &dstW, int &dstH)
{
    if (tilePx >= 2 && srcW == tiles * tilePx && srcH == tiles * tilePx)
    {
        const int dstTile = tilePx / 2;
        dstW = tiles * dstTile;
        dstH = tiles * dstTile;
        dst.assign(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4u, 0);
        for (int ty = 0; ty < tiles; ++ty)
        {
            for (int tx = 0; tx < tiles; ++tx)
            {
                for (int y = 0; y < dstTile; ++y)
                {
                    for (int x = 0; x < dstTile; ++x)
                    {
                        const int sx = tx * tilePx + x * 2;
                        const int sy = ty * tilePx + y * 2;
                        const unsigned char *p00 = src.data() + (static_cast<size_t>(sy) * srcW + sx) * 4u;
                        const unsigned char *p10 = p00 + 4;
                        const unsigned char *p01 = p00 + static_cast<size_t>(srcW) * 4u;
                        const unsigned char *p11 = p01 + 4;
                        unsigned char *out =
                            dst.data() + (static_cast<size_t>(ty * dstTile + y) * dstW + (tx * dstTile + x)) * 4u;
                        boxFilter2x2(p00, p10, p01, p11, out);
                    }
                }
            }
        }
        return;
    }

    dstW = std::max(1, srcW / 2);
    dstH = std::max(1, srcH / 2);
    dst.assign(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 4u, 0);
    for (int y = 0; y < dstH; ++y)
    {
        for (int x = 0; x < dstW; ++x)
        {
            const int sx = std::min(srcW - 1, x * 2);
            const int sy = std::min(srcH - 1, y * 2);
            const int sx1 = std::min(srcW - 1, sx + 1);
            const int sy1 = std::min(srcH - 1, sy + 1);
            const unsigned char *p00 = src.data() + (static_cast<size_t>(sy) * srcW + sx) * 4u;
            const unsigned char *p10 = src.data() + (static_cast<size_t>(sy) * srcW + sx1) * 4u;
            const unsigned char *p01 = src.data() + (static_cast<size_t>(sy1) * srcW + sx) * 4u;
            const unsigned char *p11 = src.data() + (static_cast<size_t>(sy1) * srcW + sx1) * 4u;
            boxFilter2x2(p00, p10, p01, p11, dst.data() + (static_cast<size_t>(y) * dstW + x) * 4u);
        }
    }
}
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
    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 1.0f)
    {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso));
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int levelW = width;
    int levelH = height;
    int level = 0;
    while (true)
    {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, levelW, levelH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        if (levelW == 1 && levelH == 1)
        {
            break;
        }
        levelW = std::max(1, levelW / 2);
        levelH = std::max(1, levelH / 2);
        ++level;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
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
    const unsigned char wanted = wantedFlags(mZoom, tileX, tileY, client.basemapLayer());
    unsigned char &have = mSlot[static_cast<size_t>(dy * mTiles + dx)];
    if (wanted == have)
    {
        return false;
    }

    [[maybe_unused]] bool baseFromParent = false;
    [[maybe_unused]] bool overlayFromParent = false;
    SDL_Surface *base = loadNativeOrParent(mZoom, tileX, tileY, client.basemapLayer(), baseFromParent);
    SDL_Surface *overlay = loadNativeOrParent(mZoom, tileX, tileY, "openaip", overlayFromParent);

    SDL_Rect dest{dx * kTilePx, dy * kTilePx, kTilePx, kTilePx};
    if (base != nullptr)
    {
        SDL_SetSurfaceBlendMode(base, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(base, nullptr, mSurface, &dest);
        SDL_FreeSurface(base);
    }
    else
    {
        SDL_FillRect(mSurface, &dest, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
    }
    if (overlay != nullptr)
    {
        SDL_SetSurfaceBlendMode(overlay, SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(overlay, nullptr, mSurface, &dest);
        SDL_FreeSurface(overlay);
    }
    have = wanted;
    return true;
}

void OpenAipAtlas::featherSeams(SDL_Surface *dest) const
{
    if (dest == nullptr)
    {
        return;
    }
    auto hasContent = [&](int dx, int dy) {
        return (mSlot[static_cast<size_t>(dy * mTiles + dx)] & kHasAnyBase) != 0;
    };
    for (int ty = 0; ty < mTiles; ++ty)
    {
        for (int tx = 0; tx < mTiles - 1; ++tx)
        {
            if (hasContent(tx, ty) && hasContent(tx + 1, ty))
            {
                featherVertical(dest, (tx + 1) * kTilePx, ty * kTilePx, (ty + 1) * kTilePx);
            }
        }
    }
    for (int ty = 0; ty < mTiles - 1; ++ty)
    {
        for (int tx = 0; tx < mTiles; ++tx)
        {
            if (hasContent(tx, ty) && hasContent(tx, ty + 1))
            {
                featherHorizontal(dest, (ty + 1) * kTilePx, tx * kTilePx, (tx + 1) * kTilePx);
            }
        }
    }
}

void OpenAipAtlas::uploadMipmaps(SDL_Surface *src)
{
    std::vector<unsigned char> pixels;
    copySurfaceRgba(src, pixels);
    int srcW = src->w;
    int srcH = src->h;
    int tilePx = kTilePx;
    int level = 1;
    while (srcW > 1 || srcH > 1)
    {
        std::vector<unsigned char> dst;
        int dstW = 0;
        int dstH = 0;
        downsamplePerTile(pixels, srcW, srcH, mTiles, tilePx, dst, dstW, dstH);
        glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, dstW, dstH, GL_RGBA, GL_UNSIGNED_BYTE, dst.data());
        pixels.swap(dst);
        srcW = dstW;
        srcH = dstH;
        if (tilePx >= 2)
        {
            tilePx /= 2;
        }
        ++level;
    }
}

void OpenAipAtlas::upload()
{
    if (mTexture == 0 || mSurface == nullptr)
    {
        return;
    }
    SDL_Surface *feathered = SDL_ConvertSurfaceFormat(mSurface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_Surface *src = mSurface;
    if (feathered != nullptr)
    {
        featherSeams(feathered);
        src = feathered;
    }
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int rowPixels = src->pitch / 4;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rowPixels);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src->w, src->h, GL_RGBA, GL_UNSIGNED_BYTE, src->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    uploadMipmaps(src);
    if (feathered != nullptr)
    {
        SDL_FreeSurface(feathered);
    }
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
