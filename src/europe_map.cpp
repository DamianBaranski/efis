#include "europe_map.h"

#include <SDL.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
constexpr uint32_t kLandRgba = 0xC8C8C8B4u;
constexpr uint32_t kStrokeRgba = 0xF2F2F2EEu;
constexpr uint32_t kSelectRgba = 0x4DA3FFB0u;
constexpr float kClipEps = 0.04f;
constexpr int kFillTexW = 1024;
const char *kFillTexName = "efis-europe-land";

struct TexPt
{
    float x = 0.0f;
    float y = 0.0f;
};

void fillRing(SDL_Surface *surface, const TexPt *pts, int count, Uint32 pixel)
{
    if (surface == nullptr || pts == nullptr || count < 3)
    {
        return;
    }
    const int w = surface->w;
    const int h = surface->h;
    std::vector<float> hits;
    hits.reserve(static_cast<size_t>(count));
    for (int y = 0; y < h; ++y)
    {
        const float scan = static_cast<float>(y) + 0.5f;
        hits.clear();
        int prev = count - 1;
        for (int i = 0; i < count; ++i)
        {
            const float y0 = pts[prev].y;
            const float y1 = pts[i].y;
            const bool cross = (y0 <= scan && y1 > scan) || (y1 <= scan && y0 > scan);
            if (cross)
            {
                const float x0 = pts[prev].x;
                const float x1 = pts[i].x;
                const float t = (scan - y0) / (y1 - y0);
                hits.push_back(x0 + t * (x1 - x0));
            }
            prev = i;
        }
        if (hits.size() < 2)
        {
            continue;
        }
        std::sort(hits.begin(), hits.end());
        Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) + y * surface->pitch);
        for (size_t i = 0; i + 1 < hits.size(); i += 2)
        {
            int x0 = static_cast<int>(std::floor(hits[i] + 0.5f));
            int x1 = static_cast<int>(std::floor(hits[i + 1] + 0.5f));
            if (x0 < 0)
            {
                x0 = 0;
            }
            if (x1 > w)
            {
                x1 = w;
            }
            for (int x = x0; x < x1; ++x)
            {
                row[x] = pixel;
            }
        }
    }
}
}

EuropeMap::EuropeMap(Screen &screen) : mLabel(screen)
{
    mStroke.setColor(std::to_string(kStrokeRgba), kStrokeRgba);
}

VertexTexture EuropeMap::vert(float x, float y, float u, float v)
{
    VertexTexture out{};
    out.vertex.x = x;
    out.vertex.y = y;
    out.vertex.z = 0.0f;
    out.textureCoord.x = u;
    out.textureCoord.y = v;
    out.geoCoord.x = 0.0f;
    out.geoCoord.y = 0.0f;
    return out;
}

void EuropeMap::layout(int contentX, int contentY, int contentW, int contentH, int screenW, int screenH)
{
    if (contentX == mContentX && contentY == mContentY && contentW == mContentW && contentH == mContentH &&
        screenW == mScreenW && screenH == mScreenH && mReady)
    {
        return;
    }
    mContentX = contentX;
    mContentY = contentY;
    mContentW = std::max(1, contentW);
    mContentH = std::max(1, contentH);
    mScreenW = std::max(1, screenW);
    mScreenH = std::max(1, screenH);
    setOrtho();
    project();
    rasterFill();
    rebuildQuad();
    rebuildStroke();
    rebuildLabel();
    mReady = true;
}

void EuropeMap::setOrtho()
{
    mMvp = glm::mat4(1.0f);
    mMvp[0][0] = 2.0f / static_cast<float>(mScreenW);
    mMvp[1][1] = 2.0f / static_cast<float>(mScreenH);
    mMvp[3][0] = -1.0f;
    mMvp[3][1] = -1.0f;
    mLand.setMvpMatrix(mMvp);
    mStroke.setMvpMatrix(mMvp);
}

void EuropeMap::project()
{
    const int pad = std::max(10, std::min(mContentW, mContentH) / 40);
    const int labelH = std::clamp(mContentH / 16, 22, 36);
    const float innerW = static_cast<float>(std::max(1, mContentW - 2 * pad));
    const float innerH = static_cast<float>(std::max(1, mContentH - 2 * pad - labelH));
    const float spanLon = kEuropeLon1 - kEuropeLon0;
    const float spanLat = kEuropeLat1 - kEuropeLat0;
    const float geoAspect = spanLon / spanLat;
    const float innerAspect = innerW / innerH;
    float mapW = innerW;
    float mapH = innerH;
    if (innerAspect > geoAspect)
    {
        mapW = innerH * geoAspect;
    }
    else
    {
        mapH = innerW / geoAspect;
    }
    const float mapSdlX = static_cast<float>(mContentX + pad) + (innerW - mapW) * 0.5f;
    const float mapSdlY = static_cast<float>(mContentY + pad) + (innerH - mapH) * 0.5f;
    mMapX = mapSdlX;
    mMapW = mapW;
    mMapH = mapH;
    mMapY = static_cast<float>(mScreenH) - (mapSdlY + mapH);

    mProj.resize(static_cast<size_t>(kEuropeVertexCount));
    for (int i = 0; i < kEuropeVertexCount; ++i)
    {
        const float lon = kEuropeLonLat[i * 2];
        const float lat = kEuropeLonLat[i * 2 + 1];
        mProj[static_cast<size_t>(i)].x = mMapX + (lon - kEuropeLon0) / spanLon * mMapW;
        mProj[static_cast<size_t>(i)].y = mMapY + (lat - kEuropeLat0) / spanLat * mMapH;
    }
}

bool EuropeMap::clipWall(int vertexA, int vertexB)
{
    const float lonA = kEuropeLonLat[vertexA * 2];
    const float latA = kEuropeLonLat[vertexA * 2 + 1];
    const float lonB = kEuropeLonLat[vertexB * 2];
    const float latB = kEuropeLonLat[vertexB * 2 + 1];
    const bool left = std::fabs(lonA - kEuropeLon0) < kClipEps && std::fabs(lonB - kEuropeLon0) < kClipEps;
    const bool right = std::fabs(lonA - kEuropeLon1) < kClipEps && std::fabs(lonB - kEuropeLon1) < kClipEps;
    const bool bottom = std::fabs(latA - kEuropeLat0) < kClipEps && std::fabs(latB - kEuropeLat0) < kClipEps;
    const bool top = std::fabs(latA - kEuropeLat1) < kClipEps && std::fabs(latB - kEuropeLat1) < kClipEps;
    return left || right || bottom || top;
}

void EuropeMap::rasterFill()
{
    const float spanLon = kEuropeLon1 - kEuropeLon0;
    const float spanLat = kEuropeLat1 - kEuropeLat0;
    const int texW = kFillTexW;
    const int texH = std::max(1, static_cast<int>(std::lround(static_cast<double>(texW) * spanLat / spanLon)));
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, texW, texH, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr)
    {
        return;
    }
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 0, 0, 0, 0));
    const Uint32 land = SDL_MapRGBA(surface->format, (kLandRgba >> 24) & 0xFF, (kLandRgba >> 16) & 0xFF,
                                    (kLandRgba >> 8) & 0xFF, kLandRgba & 0xFF);
    const Uint32 select = SDL_MapRGBA(surface->format, (kSelectRgba >> 24) & 0xFF, (kSelectRgba >> 16) & 0xFF,
                                      (kSelectRgba >> 8) & 0xFF, kSelectRgba & 0xFF);

    std::vector<TexPt> texPts(static_cast<size_t>(kEuropeVertexCount));
    const float sx = static_cast<float>(texW);
    const float sy = static_cast<float>(texH);
    for (int i = 0; i < kEuropeVertexCount; ++i)
    {
        const float lon = kEuropeLonLat[i * 2];
        const float lat = kEuropeLonLat[i * 2 + 1];
        texPts[static_cast<size_t>(i)].x = (lon - kEuropeLon0) / spanLon * sx;
        texPts[static_cast<size_t>(i)].y = (lat - kEuropeLat0) / spanLat * sy;
    }

    auto paintCountry = [&](int country, Uint32 pixel) {
        if (country < 0 || country >= kEuropeCountryCount)
        {
            return;
        }
        const EuropeCountry &item = kEuropeCountries[country];
        for (int r = 0; r < item.rings; ++r)
        {
            const EuropeRing &ring = kEuropeRings[item.ring0 + r];
            fillRing(surface, texPts.data() + ring.vertex0, ring.count, pixel);
        }
    };

    for (int c = 0; c < kEuropeCountryCount; ++c)
    {
        paintCountry(c, land);
    }
    paintCountry(mSelected, select);

    mLand.setTexture(kFillTexName, surface);
}

void EuropeMap::rebuildQuad()
{
    Triangles mesh;
    mesh.material = kFillTexName;
    const float x0 = mMapX;
    const float y0 = mMapY;
    const float x1 = mMapX + mMapW;
    const float y1 = mMapY + mMapH;
    mesh.vertex = {vert(x0, y0, 0.0f, 0.0f), vert(x1, y0, 1.0f, 0.0f), vert(x0, y1, 0.0f, 1.0f),
                   vert(x1, y1, 1.0f, 1.0f)};
    mesh.indices = {0, 1, 2, 2, 1, 3};
    mLand.clearGeometry();
    mLand.setTriangles({std::move(mesh)});
}

void EuropeMap::rebuildStroke()
{
    Triangles mesh;
    mesh.material = std::to_string(kStrokeRgba);
    const float half = std::max(1.25f, static_cast<float>(mContentH) / 480.0f);
    mesh.vertex.reserve(static_cast<size_t>(kEuropeVertexCount) * 4u);
    mesh.indices.reserve(static_cast<size_t>(kEuropeVertexCount) * 6u);
    for (int r = 0; r < kEuropeRingCount; ++r)
    {
        const EuropeRing &ring = kEuropeRings[r];
        if (ring.count < 2)
        {
            continue;
        }
        for (int i = 0; i < ring.count; ++i)
        {
            const int ia = ring.vertex0 + i;
            const int ib = ring.vertex0 + ((i + 1) % ring.count);
            if (clipWall(ia, ib))
            {
                continue;
            }
            const Point &a = mProj[static_cast<size_t>(ia)];
            const Point &b = mProj[static_cast<size_t>(ib)];
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::hypot(dx, dy);
            if (len < 0.25f)
            {
                continue;
            }
            const float nx = -dy / len * half;
            const float ny = dx / len * half;
            const unsigned base = static_cast<unsigned>(mesh.vertex.size());
            mesh.vertex.push_back(vert(a.x + nx, a.y + ny, 0.5f, 0.5f));
            mesh.vertex.push_back(vert(a.x - nx, a.y - ny, 0.5f, 0.5f));
            mesh.vertex.push_back(vert(b.x + nx, b.y + ny, 0.5f, 0.5f));
            mesh.vertex.push_back(vert(b.x - nx, b.y - ny, 0.5f, 0.5f));
            mesh.indices.push_back(base + 0);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 2);
            mesh.indices.push_back(base + 1);
            mesh.indices.push_back(base + 3);
        }
    }
    mStroke.clearGeometry();
    if (!mesh.indices.empty())
    {
        mStroke.setTriangles({std::move(mesh)});
    }
}

void EuropeMap::rebuildLabel()
{
    mLabelOn = false;
    if (mSelected < 0 || mSelected >= kEuropeCountryCount)
    {
        return;
    }
    const EuropeCountry &country = kEuropeCountries[mSelected];
    const float font = static_cast<float>(std::clamp(mContentH / 22, 16, 28));
    const float cx = static_cast<float>(mContentX + mContentW / 2);
    const float cy = static_cast<float>(mScreenH - (mContentY + mContentH) + std::max(14, mContentH / 28));
    const std::string text = std::string(country.iso) + "  " + country.name;
    const std::string key = std::string("efis-eumap-") + country.iso;
    mLabel.drawTextCentered(text, font, cx, cy, 0xFFFFFFFFu, key.c_str());
    mLabelOn = true;
}

void EuropeMap::render()
{
    if (!mReady)
    {
        return;
    }
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);
    glScissor(mContentX, mScreenH - mContentY - mContentH, mContentW, mContentH);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mLand.setMvpMatrix(mMvp);
    mLand.render();
    mStroke.setMvpMatrix(mMvp);
    mStroke.render();

    if (mLabelOn)
    {
        const glm::mat4 identity(1.0f);
        mLabel.setTransformationMatrix(identity);
        mLabel.render();
    }
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
}

bool EuropeMap::ringContains(float x, float y, const Point *pts, int count)
{
    bool inside = false;
    int j = count - 1;
    for (int i = 0; i < count; ++i)
    {
        const float yi = pts[i].y;
        const float yj = pts[j].y;
        if ((yi > y) != (yj > y))
        {
            const float xi = pts[i].x;
            const float xj = pts[j].x;
            const float atX = (xj - xi) * (y - yi) / (yj - yi + (yj == yi ? 1e-6f : 0.0f)) + xi;
            if (x < atX)
            {
                inside = !inside;
            }
        }
        j = i;
    }
    return inside;
}

int EuropeMap::pickCountry(float glX, float glY) const
{
    for (int c = 0; c < kEuropeCountryCount; ++c)
    {
        const EuropeCountry &country = kEuropeCountries[c];
        for (int r = 0; r < country.rings; ++r)
        {
            const EuropeRing &ring = kEuropeRings[country.ring0 + r];
            if (ring.count < 3)
            {
                continue;
            }
            if (ringContains(glX, glY, mProj.data() + ring.vertex0, ring.count))
            {
                return c;
            }
        }
    }
    return -1;
}

bool EuropeMap::hit(int sdlX, int sdlY)
{
    if (!mReady || mContentW <= 0)
    {
        return false;
    }
    if (sdlX < mContentX || sdlY < mContentY || sdlX >= mContentX + mContentW || sdlY >= mContentY + mContentH)
    {
        return false;
    }
    const float glX = static_cast<float>(sdlX);
    const float glY = static_cast<float>(mScreenH - sdlY);
    const int country = pickCountry(glX, glY);
    if (country != mSelected)
    {
        mSelected = country;
        rasterFill();
        rebuildLabel();
    }
    return true;
}
