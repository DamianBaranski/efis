#include "europe_map.h"

#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace
{
constexpr uint32_t kLandRgba = 0xC8C8C8B4u;
constexpr uint32_t kStrokeRgba = 0xF2F2F2EEu;
constexpr uint32_t kSelectRgba = 0x4DA3FFB0u;
constexpr float kClipEps = 0.04f;
}

EuropeMap::EuropeMap(Screen &screen) : mLabel(screen)
{
    mLand.setColor(std::to_string(kLandRgba), kLandRgba);
    mStroke.setColor(std::to_string(kStrokeRgba), kStrokeRgba);
    mSelect.setColor(std::to_string(kSelectRgba), kSelectRgba);
}

VertexTexture EuropeMap::vert(float x, float y)
{
    VertexTexture out{};
    out.vertex.x = x;
    out.vertex.y = y;
    out.vertex.z = 0.0f;
    out.textureCoord.x = 0.0f;
    out.textureCoord.y = 0.0f;
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
    rebuildLand();
    rebuildStroke();
    rebuildSelect();
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
    mSelect.setMvpMatrix(mMvp);
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
    const float mapGlY0 = static_cast<float>(mScreenH) - (mapSdlY + mapH);

    mProj.resize(static_cast<size_t>(kEuropeVertexCount));
    for (int i = 0; i < kEuropeVertexCount; ++i)
    {
        const float lon = kEuropeLonLat[i * 2];
        const float lat = kEuropeLonLat[i * 2 + 1];
        mProj[static_cast<size_t>(i)].x = mapSdlX + (lon - kEuropeLon0) / spanLon * mapW;
        mProj[static_cast<size_t>(i)].y = mapGlY0 + (lat - kEuropeLat0) / spanLat * mapH;
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

void EuropeMap::rebuildLand()
{
    Triangles mesh;
    mesh.material = std::to_string(kLandRgba);
    mesh.vertex.resize(static_cast<size_t>(kEuropeVertexCount));
    for (int i = 0; i < kEuropeVertexCount; ++i)
    {
        mesh.vertex[static_cast<size_t>(i)] = vert(mProj[static_cast<size_t>(i)].x, mProj[static_cast<size_t>(i)].y);
    }
    mesh.indices.assign(kEuropeIndex, kEuropeIndex + kEuropeIndexCount);
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
            mesh.vertex.push_back(vert(a.x + nx, a.y + ny));
            mesh.vertex.push_back(vert(a.x - nx, a.y - ny));
            mesh.vertex.push_back(vert(b.x + nx, b.y + ny));
            mesh.vertex.push_back(vert(b.x - nx, b.y - ny));
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

void EuropeMap::rebuildSelect()
{
    mSelect.clearGeometry();
    if (mSelected < 0 || mSelected >= kEuropeCountryCount)
    {
        return;
    }
    const EuropeCountry &country = kEuropeCountries[mSelected];
    Triangles mesh;
    mesh.material = std::to_string(kSelectRgba);
    mesh.vertex.resize(static_cast<size_t>(kEuropeVertexCount));
    for (int i = 0; i < kEuropeVertexCount; ++i)
    {
        mesh.vertex[static_cast<size_t>(i)] = vert(mProj[static_cast<size_t>(i)].x, mProj[static_cast<size_t>(i)].y);
    }
    const unsigned *begin = kEuropeIndex + country.tri0 * 3;
    mesh.indices.assign(begin, begin + country.tris * 3);
    if (!mesh.indices.empty())
    {
        mSelect.setTriangles({std::move(mesh)});
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
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glClear(GL_DEPTH_BUFFER_BIT);

    glm::mat4 landMvp = mMvp;
    landMvp[3][2] = 0.0f;
    mLand.setMvpMatrix(landMvp);
    glDisable(GL_BLEND);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    mLand.render();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_EQUAL);
    glDepthMask(GL_FALSE);
    mLand.render();

    glm::mat4 selectMvp = mMvp;
    selectMvp[3][2] = -0.02f;
    mSelect.setMvpMatrix(selectMvp);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    mSelect.render();

    glm::mat4 strokeMvp = mMvp;
    strokeMvp[3][2] = -0.04f;
    mStroke.setMvpMatrix(strokeMvp);
    mStroke.render();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
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
        rebuildSelect();
        rebuildLabel();
    }
    return true;
}
