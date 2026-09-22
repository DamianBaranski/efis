#include "sat_clipmap.h"

#include "openaip_client.h"

#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int positiveMod(int value, int mod)
{
    int m = value % mod;
    return m < 0 ? m + mod : m;
}

SDL_Surface *loadRgba(const std::string &path)
{
    SDL_Surface *tile = IMG_Load(path.c_str());
    if (tile == nullptr)
    {
        return nullptr;
    }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(tile, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(tile);
    return rgba;
}
}

SatClipmap::~SatClipmap()
{
    if (mWide.texture != 0)
    {
        glDeleteTextures(1, &mWide.texture);
    }
    if (mMid.texture != 0)
    {
        glDeleteTextures(1, &mMid.texture);
    }
    if (mFine.texture != 0)
    {
        glDeleteTextures(1, &mFine.texture);
    }
}

void SatClipmap::setChartOverlay(bool enable)
{
    if (mChartOverlay == enable)
    {
        return;
    }
    mChartOverlay = enable;
    invalidate();
}

void SatClipmap::clearSlots(Ring &ring)
{
    for (int i = 0; i < kMaxGrid * kMaxGrid; ++i)
    {
        ring.slotX[i] = -100000;
        ring.slotY[i] = -100000;
    }
}

void SatClipmap::invalidate()
{
    clearSlots(mWide);
    clearSlots(mMid);
    clearSlots(mFine);
}

int SatClipmap::midZoom() const
{
    return kMidZoom;
}

int SatClipmap::gridForZoom(int zoom)
{
    switch (zoom)
    {
    case 14:
        return 2;
    case 15:
        return 4;
    case 16:
        return 8;
    case 17:
        return 12;
    case 18:
        return 16;
    default:
        return kGrid;
    }
}

int SatClipmap::fineGrid() const
{
    return mFine.grid > 0 ? mFine.grid : kGrid;
}

int SatClipmap::midGrid() const
{
    return mMid.grid > 0 ? mMid.grid : kGrid;
}

void SatClipmap::resizeRing(Ring &ring, int grid)
{
    if (grid <= 4)
    {
        grid = 4;
    }
    else if (grid <= 8)
    {
        grid = 8;
    }
    else if (grid <= 12)
    {
        grid = 12;
    }
    else
    {
        grid = kMaxGrid;
    }
    if (ring.grid == grid)
    {
        return;
    }
    if (ring.texture != 0)
    {
        glDeleteTextures(1, &ring.texture);
        ring.texture = 0;
    }
    ring.created = false;
    ring.grid = grid;
    clearSlots(ring);
}

void SatClipmap::setDetailZoom(int zoom)
{
    zoom = std::clamp(zoom, 12, 18);
    if (zoom == mDetailZoom && mFine.zoom == zoom)
    {
        return;
    }
    mDetailZoom = zoom;
    mFine.zoom = zoom;
    clearSlots(mFine);
    std::cout << "SAT clip detail z=" << mFine.zoom << " grid=" << mFine.grid << std::endl;
}

void SatClipmap::setFineGrid(int grid)
{
    resizeRing(mFine, grid);
}

void SatClipmap::setMidZoom(int zoom)
{
    zoom = std::clamp(zoom, 9, 12);
    if (zoom == mMid.zoom)
    {
        return;
    }
    mMid.zoom = zoom;
    clearSlots(mMid);
    std::cout << "SAT clip far z=" << mMid.zoom << " grid=" << mMid.grid << std::endl;
}

void SatClipmap::setMidGrid(int grid)
{
    resizeRing(mMid, grid);
}

int SatClipmap::layerOf(const Ring &ring, int tileX, int tileY) const
{
    return positiveMod(tileX, ring.grid) + positiveMod(tileY, ring.grid) * ring.grid;
}

int SatClipmap::windowOrigin(int cameraTile, int grid)
{
    return cameraTile - grid / 2;
}

bool SatClipmap::holds(const Ring &ring, int tileX, int tileY) const
{
    const int layer = layerOf(ring, tileX, tileY);
    return ring.slotX[layer] == tileX && ring.slotY[layer] == tileY;
}

void SatClipmap::ensureRing(Ring &ring)
{
    if (ring.created)
    {
        return;
    }
    glGenTextures(1, &ring.texture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, ring.texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const int layers = ring.grid * ring.grid;
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, kTilePx, kTilePx, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    clearSlots(ring);
    ring.created = true;
    const int texMb = layers * kTilePx * kTilePx * 4 / (1024 * 1024);
    std::cout << "SAT clip z=" << ring.zoom << " grid=" << ring.grid << " layers=" << layers << " texMB=" << texMb
              << std::endl;
}

bool SatClipmap::uploadTile(Ring &ring, int tileX, int tileY)
{
    auto &client = OpenAipClient::instance();
    const std::string layerName = client.basemapLayer();
    if (!client.isCached(ring.zoom, tileX, tileY, layerName))
    {
        return false;
    }
    SDL_Surface *base = loadRgba(client.cachePath(ring.zoom, tileX, tileY, layerName));
    if (base == nullptr)
    {
        return false;
    }
    if (mChartOverlay && client.isCached(ring.zoom, tileX, tileY, "openaip"))
    {
        SDL_Surface *overlay = loadRgba(client.cachePath(ring.zoom, tileX, tileY, "openaip"));
        if (overlay != nullptr)
        {
            SDL_SetSurfaceBlendMode(overlay, SDL_BLENDMODE_BLEND);
            SDL_BlitSurface(overlay, nullptr, base, nullptr);
            SDL_FreeSurface(overlay);
        }
    }

    const int layer = layerOf(ring, tileX, tileY);
    const int bpp = base->format ? base->format->BytesPerPixel : 4;
    glBindTexture(GL_TEXTURE_2D_ARRAY, ring.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bpp > 0 ? base->pitch / bpp : 0);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, kTilePx, kTilePx, 1, GL_RGBA, GL_UNSIGNED_BYTE, base->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    SDL_FreeSurface(base);
    ring.slotX[layer] = tileX;
    ring.slotY[layer] = tileY;
    return true;
}

void SatClipmap::pump(float latitude, float longitude, int maxUploads)
{
    if (!mLogged)
    {
        int png = IMG_INIT_PNG;
        IMG_Init(png);
        if (mMid.zoom <= 0)
        {
            mMid.zoom = kMidZoom;
        }
        if (mFine.zoom <= 0)
        {
            mFine.zoom = mDetailZoom;
        }
        mLogged = true;
    }
    ensureRing(mMid);
    ensureRing(mFine);

    auto &client = OpenAipClient::instance();
    Ring *rings[2] = {&mMid, &mFine};
    for (Ring *ring : rings)
    {
        const auto center = OpenAipClient::latLonToTile(latitude, longitude, ring->zoom);
        ring->originX = windowOrigin(center.first, ring->grid);
        ring->originY = windowOrigin(center.second, ring->grid);
        client.fetchAround(latitude, longitude, ring->zoom, ring->grid / 2, true, mChartOverlay);
    }

    struct Job
    {
        int ring = 0;
        int x = 0;
        int y = 0;
        int dist = 0;
    };
    std::vector<Job> jobs;
    jobs.reserve(static_cast<size_t>(kGrid * kGrid + kMaxGrid * kMaxGrid));
    for (int r = 0; r < 2; ++r)
    {
        Ring &ring = *rings[r];
        const auto center = OpenAipClient::latLonToTile(latitude, longitude, ring.zoom);
        for (int ly = 0; ly < ring.grid; ++ly)
        {
            for (int lx = 0; lx < ring.grid; ++lx)
            {
                const int x = ring.originX + lx;
                const int y = ring.originY + ly;
                if (holds(ring, x, y))
                {
                    continue;
                }
                const int dx = x - center.first;
                const int dy = y - center.second;
                jobs.push_back({r, x, y, std::max(std::abs(dx), std::abs(dy))});
            }
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const Job &a, const Job &b) {
        if (a.ring != b.ring)
        {
            return a.ring > b.ring;
        }
        return a.dist < b.dist;
    });

    int uploaded = 0;
    for (const Job &job : jobs)
    {
        if (uploaded >= maxUploads)
        {
            break;
        }
        if (uploadTile(*rings[job.ring], job.x, job.y))
        {
            ++uploaded;
        }
    }
}

void SatClipmap::maskFor(const Ring &ring, uint32_t *words, int wordCount) const
{
    for (int i = 0; i < wordCount; ++i)
    {
        words[i] = 0;
    }
    for (int ly = 0; ly < ring.grid; ++ly)
    {
        for (int lx = 0; lx < ring.grid; ++lx)
        {
            if (!holds(ring, ring.originX + lx, ring.originY + ly))
            {
                continue;
            }
            const int bit = lx + ly * ring.grid;
            const int word = bit >> 5;
            if (word >= 0 && word < wordCount)
            {
                words[word] |= 1u << (bit & 31);
            }
        }
    }
}

SatClipmap::Progress SatClipmap::progressFor(const Ring &ring) const
{
    Progress progress;
    progress.total = ring.grid * ring.grid;
    if (ring.created)
    {
        progress.gpuBytes = static_cast<size_t>(ring.grid) * static_cast<size_t>(ring.grid) * kTilePx * kTilePx * 4u;
    }
    int done = 0;
    for (int ly = 0; ly < ring.grid; ++ly)
    {
        for (int lx = 0; lx < ring.grid; ++lx)
        {
            if (holds(ring, ring.originX + lx, ring.originY + ly))
            {
                ++done;
            }
        }
    }
    progress.done = done;
    progress.ready = ring.created && done == progress.total;
    return progress;
}

SatClipmap::Progress SatClipmap::fineProgress() const
{
    return progressFor(mFine);
}

SatClipmap::Progress SatClipmap::coarseProgress() const
{
    return progressFor(mMid);
}

bool SatClipmap::ready() const
{
    return progressFor(mMid).ready && progressFor(mFine).ready;
}

size_t SatClipmap::gpuBytes() const
{
    return progressFor(mMid).gpuBytes + progressFor(mFine).gpuBytes;
}

SatClipmap::View SatClipmap::view() const
{
    View view;
    view.active = mMid.texture != 0 || mFine.texture != 0;
    view.fineTex = mFine.texture;
    view.midTex = mMid.texture;
    view.wideTex = mWide.texture;
    view.fineOriginX = mFine.originX;
    view.fineOriginY = mFine.originY;
    view.midOriginX = mMid.originX;
    view.midOriginY = mMid.originY;
    view.wideOriginX = mWide.originX;
    view.wideOriginY = mWide.originY;
    view.fineZoom = mFine.zoom > 0 ? mFine.zoom : mDetailZoom;
    view.midZoom = mMid.zoom > 0 ? mMid.zoom : midZoom();
    view.wideZoom = mWide.zoom > 0 ? mWide.zoom : kWideZoom;
    view.fineGrid = fineGrid();
    view.midGrid = midGrid();
    maskFor(mFine, view.fineMask, kMaskWords);
    maskFor(mMid, view.midMask, kMaskWords);
    uint32_t wideWords[2] = {0, 0};
    maskFor(mWide, wideWords, 2);
    view.wideMask0 = wideWords[0];
    view.wideMask1 = wideWords[1];
    return view;
}
