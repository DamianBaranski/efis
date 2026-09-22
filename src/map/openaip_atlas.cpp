/// \file openaip_atlas.cpp
/// Stitches cached PNG tiles into one texture draped on the terrain.
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

unsigned char wantedFlags(int zoom, int tileX, int tileY, const std::string &baseLayer, bool wantBase,
                          bool wantOverlay)
{
    auto &client = OpenAipClient::instance();
    unsigned char wanted = 0;
    if (wantBase)
    {
        if (client.isCached(zoom, tileX, tileY, baseLayer))
        {
            wanted |= kHasNativeBase;
        }
        else if (parentCached(zoom, tileX, tileY, baseLayer))
        {
            wanted |= kHasParentBase;
        }
    }
    if (wantOverlay)
    {
        if (client.isCached(zoom, tileX, tileY, "openaip"))
        {
            wanted |= kHasNativeOverlay;
        }
        else if (parentCached(zoom, tileX, tileY, "openaip"))
        {
            wanted |= kHasParentOverlay;
        }
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

size_t OpenAipAtlas::cpuBytesFor(int radius)
{
    const int tiles = 2 * radius + 1;
    const int px = tiles * kTilePx;
    return static_cast<size_t>(px) * static_cast<size_t>(px) * 4u;
}

size_t OpenAipAtlas::gpuBytesFor(int radius)
{
    return cpuBytesFor(radius) * 4u / 3u;
}

OpenAipAtlas::OpenAipAtlas(int zoom, int radius)
    : mTiles(2 * radius + 1), mN(static_cast<float>(std::exp2(zoom))), mZoom(zoom), mRadius(radius),
      mSlot(static_cast<size_t>(mTiles) * static_cast<size_t>(mTiles), 0)
{
    // Wide atlases never keep a second full copy in RAM. Disk cache is the backing store.
    mReleasedCpu = mTiles * kTilePx >= 4096;
}

OpenAipAtlas::~OpenAipAtlas()
{
    if (mShiftFbo != 0)
    {
        glDeleteFramebuffers(1, &mShiftFbo);
    }
    if (mRowTex != 0)
    {
        glDeleteTextures(1, &mRowTex);
    }
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
    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (maxSize > 0 && (width > maxSize || height > maxSize))
    {
        std::cerr << "OpenAIP atlas " << width << "x" << height << " exceeds GL_MAX_TEXTURE_SIZE "
                  << maxSize << std::endl;
        return;
    }
    mUseMips = width < 4096;
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mUseMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 1.0f && mUseMips)
    {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso));
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!mUseMips)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        std::cout << "SAT atlas z=" << mZoom << " vram-only " << width << "x" << height << std::endl;
        return;
    }
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
    std::cout << "SAT atlas z=" << mZoom << " cpu+vram " << width << "x" << height << " mips=" << level << std::endl;
}

void OpenAipAtlas::releaseCpuStore()
{
    if (mSurface != nullptr)
    {
        std::cout << "SAT atlas z=" << mZoom << " released CPU store" << std::endl;
        SDL_FreeSurface(mSurface);
        mSurface = nullptr;
    }
    mReleasedCpu = true;
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

bool OpenAipAtlas::shiftGpuTiles(int dx, int dy)
{
    if (mTexture == 0 || (dx == 0 && dy == 0))
    {
        return true;
    }
    if (std::abs(dx) >= mTiles || std::abs(dy) >= mTiles)
    {
        return false;
    }
    const int texW = mTiles * kTilePx;
    if (mRowTex == 0)
    {
        glGenTextures(1, &mRowTex);
        glBindTexture(GL_TEXTURE_2D, mRowTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, kTilePx, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    if (mShiftFbo == 0)
    {
        glGenFramebuffers(1, &mShiftFbo);
    }

    GLint prevFbo = 0;
    GLint prevTex = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

    const int srcX = std::max(dx, 0) * kTilePx;
    const int dstX = std::max(-dx, 0) * kTilePx;
    const int copyW = (mTiles - std::abs(dx)) * kTilePx;
    const int rowCount = mTiles - std::abs(dy);
    const int yStep = dy >= 0 ? 1 : -1;
    const int iBegin = dy >= 0 ? 0 : rowCount - 1;
    const int iEnd = dy >= 0 ? rowCount : -1;

    bool ok = copyW > 0 && rowCount > 0;
    for (int i = iBegin; i != iEnd && ok; i += yStep)
    {
        const int srcY = (i + std::max(dy, 0)) * kTilePx;
        const int dstY = (i + std::max(-dy, 0)) * kTilePx;

        glBindFramebuffer(GL_FRAMEBUFFER, mShiftFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            ok = false;
            break;
        }
        glBindTexture(GL_TEXTURE_2D, mRowTex);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, dstX, 0, srcX, srcY, copyW, kTilePx);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mRowTex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            ok = false;
            break;
        }
        glBindTexture(GL_TEXTURE_2D, mTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, dstX, dstY, dstX, 0, copyW, kTilePx);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    mUseMips = false;
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    if (!ok)
    {
        std::cerr << "SAT atlas gpu-shift failed z=" << mZoom << std::endl;
    }
    return ok;
}

void OpenAipAtlas::shiftOrigin(int originX, int originY)
{
    const int dx = originX - mOriginX;
    const int dy = originY - mOriginY;
    const bool hadOrigin = mOriginX > -10000;
    mOriginX = originX;
    mOriginY = originY;
    if (dx == 0 && dy == 0)
    {
        return;
    }
    if (!hadOrigin || std::abs(dx) >= mTiles || std::abs(dy) >= mTiles)
    {
        if (mSurface != nullptr)
        {
            SDL_FillRect(mSurface, nullptr, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
        }
        std::fill(mSlot.begin(), mSlot.end(), 0);
        return;
    }

    if (mSurface != nullptr)
    {
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
    }
    else if (!shiftGpuTiles(dx, dy))
    {
        std::fill(mSlot.begin(), mSlot.end(), 0);
        return;
    }

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
    const unsigned char wanted =
        wantedFlags(mZoom, tileX, tileY, client.basemapLayer(), mWantBasemap, mWantOverlay);
    unsigned char &have = mSlot[static_cast<size_t>(dy * mTiles + dx)];
    if (wanted == have)
    {
        return false;
    }

    [[maybe_unused]] bool baseFromParent = false;
    [[maybe_unused]] bool overlayFromParent = false;
    SDL_Surface *base =
        mWantBasemap ? loadNativeOrParent(mZoom, tileX, tileY, client.basemapLayer(), baseFromParent) : nullptr;
    SDL_Surface *overlay =
        mWantOverlay ? loadNativeOrParent(mZoom, tileX, tileY, "openaip", overlayFromParent) : nullptr;

    SDL_Surface *owned = nullptr;
    SDL_Surface *destSurf = mSurface;
    SDL_Rect dest{dx * kTilePx, dy * kTilePx, kTilePx, kTilePx};
    if (destSurf == nullptr)
    {
        owned = SDL_CreateRGBSurfaceWithFormat(0, kTilePx, kTilePx, 32, SDL_PIXELFORMAT_RGBA32);
        destSurf = owned;
        dest = {0, 0, kTilePx, kTilePx};
    }
    if (destSurf == nullptr)
    {
        SDL_FreeSurface(base);
        SDL_FreeSurface(overlay);
        return false;
    }
    if (base != nullptr)
    {
        SDL_SetSurfaceBlendMode(base, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(base, nullptr, destSurf, &dest);
        SDL_FreeSurface(base);
    }
    else
    {
        SDL_FillRect(destSurf, &dest, SDL_MapRGBA(destSurf->format, 0, 0, 0, 0));
    }
    if (overlay != nullptr)
    {
        SDL_SetSurfaceBlendMode(overlay, SDL_BLENDMODE_BLEND);
        SDL_BlitSurface(overlay, nullptr, destSurf, &dest);
        SDL_FreeSurface(overlay);
    }
    if (owned != nullptr)
    {
        uploadTileFrom(owned, dx, dy);
        SDL_FreeSurface(owned);
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
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    std::vector<unsigned char> packed;
    copySurfaceRgba(src, packed);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src->w, src->h, GL_RGBA, GL_UNSIGNED_BYTE, packed.data());
    uploadMipmaps(src);
    if (feathered != nullptr)
    {
        SDL_FreeSurface(feathered);
    }
}

void OpenAipAtlas::setLayers(bool basemap, bool overlay)
{
    if (mWantBasemap == basemap && mWantOverlay == overlay)
    {
        return;
    }
    if (!basemap && !overlay)
    {
        mWantBasemap = false;
        mWantOverlay = false;
        return;
    }
    const bool keepBase = mWantBasemap && basemap && mSurface != nullptr;
    mWantBasemap = basemap;
    mWantOverlay = overlay;
    mReady = false;
    mNeedMips = true;
    mScan = 0;
    if (keepBase)
    {
        std::fill(mSlot.begin(), mSlot.end(), 0);
        return;
    }
    if (mSurface != nullptr)
    {
        SDL_FillRect(mSurface, nullptr, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
    }
    std::fill(mSlot.begin(), mSlot.end(), 0);
}

void OpenAipAtlas::uploadTileFrom(SDL_Surface *tile, int dx, int dy)
{
    if (mTexture == 0 || tile == nullptr)
    {
        return;
    }
    const int bpp = tile->format ? tile->format->BytesPerPixel : 4;
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bpp > 0 ? tile->pitch / bpp : 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, dx * kTilePx, dy * kTilePx, kTilePx, kTilePx, GL_RGBA, GL_UNSIGNED_BYTE,
                    tile->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void OpenAipAtlas::uploadTile(int dx, int dy)
{
    if (mTexture == 0 || mSurface == nullptr)
    {
        return;
    }
    const int x = dx * kTilePx;
    const int y = dy * kTilePx;
    const int bpp = mSurface->format ? mSurface->format->BytesPerPixel : 4;
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bpp > 0 ? mSurface->pitch / bpp : 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, kTilePx, kTilePx, GL_RGBA, GL_UNSIGNED_BYTE, pxAt(mSurface, x, y));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

int OpenAipAtlas::countPending() const
{
    if (mOriginX < -10000)
    {
        return mTiles * mTiles;
    }
    auto &client = OpenAipClient::instance();
    int pending = 0;
    for (int dy = 0; dy < mTiles; ++dy)
    {
        for (int dx = 0; dx < mTiles; ++dx)
        {
            const unsigned char wanted =
                wantedFlags(mZoom, mOriginX + dx, mOriginY + dy, client.basemapLayer(), mWantBasemap, mWantOverlay);
            if (wanted != mSlot[static_cast<size_t>(dy * mTiles + dx)])
            {
                ++pending;
            }
        }
    }
    return pending;
}

OpenAipAtlas::Progress OpenAipAtlas::progress() const
{
    Progress p;
    p.total = mTiles * mTiles;
    p.done = std::max(0, p.total - countPending());
    if (mSurface != nullptr)
    {
        p.cpuBytes = cpuBytesFor(mRadius);
    }
    if (mTexture != 0)
    {
        p.gpuBytes = mUseMips ? gpuBytesFor(mRadius) : cpuBytesFor(mRadius);
    }
    p.ready = mReady;
    return p;
}

void OpenAipAtlas::update(float latitude, float longitude)
{
    pump(latitude, longitude, mTiles * mTiles);
}

void OpenAipAtlas::pump(float latitude, float longitude, int maxBlits)
{
    auto &client = OpenAipClient::instance();
    mN = static_cast<float>(std::exp2(mZoom));
    client.fetchAround(latitude, longitude, mZoom, mRadius);
    ensureTexture();
    if (!mReleasedCpu)
    {
        ensureSurface();
    }
    if (mTexture == 0)
    {
        return;
    }
    if (!mReleasedCpu && mSurface == nullptr)
    {
        mReleasedCpu = true;
    }

    const std::string layer = client.basemapLayer();
    const auto center = OpenAipClient::latLonToTile(latitude, longitude, mZoom);
    const int originX = center.first - mRadius;
    const int originY = center.second - mRadius;
    if (originX != mOriginX || originY != mOriginY || layer != mBasemapLayer)
    {
        if (layer != mBasemapLayer)
        {
            if (mSurface != nullptr)
            {
                SDL_FillRect(mSurface, nullptr, SDL_MapRGBA(mSurface->format, 0, 0, 0, 0));
            }
            std::fill(mSlot.begin(), mSlot.end(), 0);
            mOriginX = originX;
            mOriginY = originY;
        }
        else
        {
            shiftOrigin(originX, originY);
        }
        mBasemapLayer = layer;
        mReady = false;
        mNeedMips = true;
        mScan = 0;
    }

    int flags = IMG_INIT_PNG;
    if ((IMG_Init(flags) & flags) == 0)
    {
        return;
    }

    const int total = mTiles * mTiles;
    int blits = 0;
    for (int i = 0; i < total && blits < maxBlits; ++i)
    {
        const int idx = (mScan + i) % total;
        const int dx = idx % mTiles;
        const int dy = idx / mTiles;
        if (blitSlot(dx, dy))
        {
            if (mSurface != nullptr)
            {
                uploadTile(dx, dy);
            }
            mNeedMips = true;
            mReady = false;
            ++blits;
            mScan = (idx + 1) % total;
        }
    }

    if (blits < maxBlits && !mReady && countPending() == 0)
    {
        if (mSurface != nullptr && mNeedMips && mUseMips)
        {
            glBindTexture(GL_TEXTURE_2D, mTexture);
            uploadMipmaps(mSurface);
        }
        mNeedMips = false;
        releaseCpuStore();
        mReady = true;
    }
}
