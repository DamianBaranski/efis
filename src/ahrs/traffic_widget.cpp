/// \file traffic_widget.cpp
/// Heading-up traffic dial. Selects a collision threat, or the closest aircraft.
#include "traffic_widget.h"
#include "data_manager_sim.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <utility>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMPerNm = 1852.0f;
constexpr float kRangeNm = 15.0f;
constexpr float kOuterPx = 118.0f;
constexpr float kKtPerMps = 1.94384449f;
constexpr float kFtPerM = 3.2808399f;
/// Miss distance and vertical gate for a collision-course target.
constexpr float kMissNm = 1.0f;
constexpr float kVertFt = 1000.0f;
constexpr float kMaxTcpaSec = 180.0f;

/// drawText packs blue, green, red, alpha from the high byte.
constexpr uint32_t kInk = 0xFFFFFFFFu;
constexpr uint32_t kRoseNum = 0xECECECFFu;
constexpr uint32_t kCyanText = 0xF2F23DFFu;
constexpr uint32_t kWhiteText = 0xF4F4F4FFu;

struct DemoTarget
{
    const char *id;
    float eastNm;
    float northNm;
    float altFt;
    float trackDeg;
    float gsKt;
    const char *category;
};

/// Fixed traffic near Mirosławice, in the same places as the HTML mockup.
constexpr DemoTarget kTargets[] = {
    {"NX211", 2.165f, -1.25f, 6000.0f, 125.0f, 94.0f, "Light"},
    {"N814Q", -3.6f, 6.235f, 9000.0f, 338.0f, 128.0f, "Light"},
    {"N17SK", 6.023f, 8.601f, 9500.0f, 28.0f, 156.0f, "Small"},
};

struct Buckets
{
    std::unordered_map<std::string, Triangles> groups;

    Triangles &get(const std::string &name)
    {
        Triangles &dst = groups[name];
        dst.material = name;
        return dst;
    }
};

void addTri(Triangles &dst, float x0, float y0, float x1, float y1, float x2, float y2)
{
    const unsigned base = static_cast<unsigned>(dst.vertex.size());
    auto put = [&](float x, float y) {
        VertexTexture v{};
        v.vertex.x = x;
        v.vertex.y = y;
        v.textureCoord.x = 0.5f;
        v.textureCoord.y = 0.5f;
        dst.vertex.push_back(v);
    };
    put(x0, y0);
    put(x1, y1);
    put(x2, y2);
    dst.indices.push_back(base);
    dst.indices.push_back(base + 1);
    dst.indices.push_back(base + 2);
}

void addQuad(Triangles &dst, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3)
{
    addTri(dst, x0, y0, x1, y1, x2, y2);
    addTri(dst, x0, y0, x2, y2, x3, y3);
}

/// Clockwise angle from up. Local +y is outward, local +x is right.
void spin(float cx, float cy, float lx, float ly, float clockwiseRad, float &ox, float &oy)
{
    const float s = std::sin(clockwiseRad);
    const float c = std::cos(clockwiseRad);
    ox = cx + lx * c + ly * s;
    oy = cy - lx * s + ly * c;
}

void addTick(Triangles &dst, float cx, float cy, float clockwiseRad, float r0, float r1, float halfW)
{
    float x0, y0, x1, y1, x2, y2, x3, y3;
    spin(cx, cy, -halfW, r0, clockwiseRad, x0, y0);
    spin(cx, cy, halfW, r0, clockwiseRad, x1, y1);
    spin(cx, cy, halfW, r1, clockwiseRad, x2, y2);
    spin(cx, cy, -halfW, r1, clockwiseRad, x3, y3);
    addQuad(dst, x0, y0, x1, y1, x2, y2, x3, y3);
}

void addDisc(Triangles &dst, float cx, float cy, float radius, int slices)
{
    for (int i = 0; i < slices; ++i)
    {
        const float a0 = (static_cast<float>(i) / static_cast<float>(slices)) * 2.0f * kPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(slices)) * 2.0f * kPi;
        addTri(dst, cx, cy, cx + radius * std::cos(a0), cy + radius * std::sin(a0), cx + radius * std::cos(a1),
               cy + radius * std::sin(a1));
    }
}

void addAnnulus(Triangles &dst, float cx, float cy, float inner, float outer, int slices)
{
    for (int i = 0; i < slices; ++i)
    {
        const float a0 = (static_cast<float>(i) / static_cast<float>(slices)) * 2.0f * kPi;
        const float a1 = (static_cast<float>(i + 1) / static_cast<float>(slices)) * 2.0f * kPi;
        const float c0 = std::cos(a0);
        const float s0 = std::sin(a0);
        const float c1 = std::cos(a1);
        const float s1 = std::sin(a1);
        addQuad(dst, cx + inner * c0, cy + inner * s0, cx + outer * c0, cy + outer * s0, cx + outer * c1,
                cy + outer * s1, cx + inner * c1, cy + inner * s1);
    }
}

float wrap360(float deg)
{
    float x = std::fmod(deg, 360.0f);
    if (x < 0.0f)
    {
        x += 360.0f;
    }
    return x;
}

float wrap180(float deg)
{
    float x = std::fmod(deg + 180.0f, 360.0f);
    if (x < 0.0f)
    {
        x += 360.0f;
    }
    return x - 180.0f;
}

float radClockwise(float degFromUp)
{
    return degFromUp * kPi / 180.0f;
}

void targetLatLon(const DemoTarget &target, double &lat, double &lon)
{
    const double lat0 = DataManagerSim::kEpmrLatitude;
    const double lon0 = DataManagerSim::kEpmrLongitude;
    lat = lat0 + static_cast<double>(target.northNm) * kMPerNm / 111320.0;
    lon = lon0 + static_cast<double>(target.eastNm) * kMPerNm / (111320.0 * std::cos(lat0 * kPi / 180.0));
}
} // namespace

TrafficWidget::TrafficWidget(Frame &frame, IDataManager &data, Slot slot)
    : IWidget(frame), mData(data), mSlot(slot), mShader()
{
}

void TrafficWidget::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void TrafficWidget::prepareColors()
{
    if (mColorsReady)
    {
        return;
    }
    mShader.setColor("tr-plate", 0x06080A75u);
    mShader.setColor("tr-white", 0xF4F4F4FFu);
    mShader.setColor("tr-major", 0xD0D0D0FFu);
    mShader.setColor("tr-minor", 0x7D7D7DFFu);
    mShader.setColor("tr-arc", 0xFFFFFF2Eu);
    mShader.setColor("tr-cyan", 0x3DF2F2FFu);
    mShader.setColor("tr-halo", 0xA8A8A8B8u);
    mColorsReady = true;
}

void TrafficWidget::layout()
{
    const int w = std::max(1, mScreen.getWidth());
    const int h = std::max(1, mScreen.getHeight());
    if (w == mLayoutW && h == mLayoutH)
    {
        return;
    }
    mLayoutW = w;
    mLayoutH = h;

    const float column = std::min(static_cast<float>(h) / 3.0f, static_cast<float>(w) * 0.28f);
    const float inset = std::max(4.0f, column * 0.04f);
    const float outer = std::max(36.0f, column * 0.5f - inset);
    mScale = outer / 200.0f;
    const bool right = mSlot == Slot::RightBottom || mSlot == Slot::RightTop;
    const bool top = mSlot == Slot::LeftTop || mSlot == Slot::RightTop;
    mCx = right ? static_cast<float>(w) - outer - inset : outer + inset;
    mCy = outer + inset + (top ? outer * 2.0f + inset : 0.0f);
}

void TrafficWidget::paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size,
                               uint32_t color, float x, float y, float rotRad)
{
    if (!glyph.draw)
    {
        glyph.draw = std::make_unique<Render2D>(mScreen);
    }
    const int px = std::max(8, static_cast<int>(std::lround(size)));
    if (glyph.text != text || std::fabs(glyph.size - static_cast<float>(px)) > 0.5f)
    {
        glyph.text = text;
        glyph.size = static_cast<float>(px);
        glyph.draw->drawTextCentered(text, glyph.size, 0.0f, 0.0f, color, cacheId);
    }
    glm::mat4 xform(1.0f);
    xform = glm::translate(xform, glm::vec3(x, y, 0.0f));
    xform = glm::rotate(xform, rotRad, glm::vec3(0.0f, 0.0f, 1.0f));
    glyph.draw->setTransformationMatrix(xform);
}

void TrafficWidget::render()
{
    if (!mEnabled)
    {
        return;
    }
    layout();
    prepareColors();

    const AttitudeData &attitude = mData.getAttitudeData();
    const DynamicsData &dynamics = mData.getDynamicsData();
    const LocationData &place = mData.getLocationData();
    const float headingDeg = wrap360(attitude.heading * 180.0f / kPi);
    const float ownAltFt = place.altitude * kFtPerM;
    const float ownGsKt = dynamics.airspeed * kKtPerMps;
    const float ownH = attitude.heading;
    const float ownVe = std::sin(ownH) * ownGsKt;
    const float ownVn = std::cos(ownH) * ownGsKt;

    struct Placed
    {
        float distNm = 0.0f;
        float relAltFt = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float trackRel = 0.0f;
        bool onScale = false;
    };
    Placed placed[3];
    int threat = -1;
    float threatTcpa = kMaxTcpaSec;
    int closest = -1;
    float closestDist = 1.0e9f;

    for (int i = 0; i < 3; ++i)
    {
        const DemoTarget &target = kTargets[i];
        double lat = 0.0;
        double lon = 0.0;
        targetLatLon(target, lat, lon);
        const double dNorth = (lat - place.latitude) * 111320.0;
        const double dEast = (lon - place.longitude) * 111320.0 * std::cos(place.latitude * kPi / 180.0);
        const float de = static_cast<float>(dEast / kMPerNm);
        const float dn = static_cast<float>(dNorth / kMPerNm);
        const float dist = std::hypot(de, dn);
        placed[i].distNm = dist;
        placed[i].relAltFt = target.altFt - ownAltFt;
        placed[i].onScale = dist <= kRangeNm * 1.04f && dist > 0.05f;
        if (!placed[i].onScale)
        {
            continue;
        }
        if (dist < closestDist)
        {
            closestDist = dist;
            closest = i;
        }
        const float tr = target.trackDeg * kPi / 180.0f;
        const float ve = std::sin(tr) * target.gsKt - ownVe;
        const float vn = std::cos(tr) * target.gsKt - ownVn;
        const float vv = ve * ve + vn * vn;
        if (vv >= 1.0f)
        {
            const float tcpaH = -(de * ve + dn * vn) / vv;
            if (tcpaH > 0.0f)
            {
                const float tcpaSec = tcpaH * 3600.0f;
                const float miss = std::hypot(de + ve * tcpaH, dn + vn * tcpaH);
                const float dAlt = (target.altFt + 0.0f) - ownAltFt;
                if (tcpaSec <= kMaxTcpaSec && miss <= kMissNm && std::fabs(dAlt) <= kVertFt && tcpaSec < threatTcpa)
                {
                    threatTcpa = tcpaSec;
                    threat = i;
                }
            }
        }

        const float rel = radClockwise(wrap360(static_cast<float>(std::atan2(dEast, dNorth) * 180.0 / kPi) - headingDeg));
        const float rpx = (dist / kRangeNm) * kOuterPx * mScale;
        placed[i].x = mCx + rpx * std::sin(rel);
        placed[i].y = mCy + rpx * std::cos(rel);
        placed[i].trackRel = radClockwise(target.trackDeg - headingDeg);
    }

    const int selected = threat >= 0 ? threat : closest;

    {
        Buckets mesh;
        const float cx = mCx;
        const float cy = mCy;
        const float s = mScale;
        addDisc(mesh.get("tr-plate"), cx, cy, 168.0f * s, 48);
        const float rings[3] = {39.33f, 78.67f, 118.0f};
        for (float radius : rings)
        {
            addAnnulus(mesh.get("tr-major"), cx, cy, (radius - 0.7f) * s, (radius + 0.7f) * s, 48);
        }
        addAnnulus(mesh.get("tr-arc"), cx, cy, 187.4f * s, 188.6f * s, 64);
        for (int deg = 0; deg < 360; deg += 5)
        {
            const float shown = wrap180(static_cast<float>(deg) - headingDeg);
            const char *mat = "tr-minor";
            float inner = 180.0f;
            float width = 1.15f;
            if (deg % 30 == 0)
            {
                mat = "tr-white";
                inner = 164.0f;
                width = 2.2f;
            }
            else if (deg % 10 == 0)
            {
                mat = "tr-major";
                inner = 172.0f;
                width = 1.7f;
            }
            addTick(mesh.get(mat), cx, cy, radClockwise(shown), inner * s, 188.0f * s, width * s * 0.5f);
        }
        addTri(mesh.get("tr-white"), cx, cy + 200.0f * s, cx - 7.0f * s, cy + 186.0f * s, cx + 7.0f * s,
               cy + 186.0f * s);
        for (int i = 0; i < 3; ++i)
        {
            if (!placed[i].onScale)
            {
                continue;
            }
            if (i == selected)
            {
                addDisc(mesh.get("tr-halo"), placed[i].x, placed[i].y, 16.0f * s, 20);
            }
            float x0, y0, x1, y1, x2, y2, x3, y3;
            const float ang = placed[i].trackRel;
            spin(placed[i].x, placed[i].y, 0.0f, 9.0f * s, ang, x0, y0);
            spin(placed[i].x, placed[i].y, -7.0f * s, -8.0f * s, ang, x1, y1);
            spin(placed[i].x, placed[i].y, 7.0f * s, -8.0f * s, ang, x2, y2);
            addTri(mesh.get("tr-cyan"), x0, y0, x1, y1, x2, y2);
            spin(placed[i].x, placed[i].y, -1.1f * s, -8.0f * s, ang, x0, y0);
            spin(placed[i].x, placed[i].y, 1.1f * s, -8.0f * s, ang, x1, y1);
            spin(placed[i].x, placed[i].y, 1.1f * s, -15.0f * s, ang, x2, y2);
            spin(placed[i].x, placed[i].y, -1.1f * s, -15.0f * s, ang, x3, y3);
            addQuad(mesh.get("tr-cyan"), x0, y0, x1, y1, x2, y2, x3, y3);
        }
        addTri(mesh.get("tr-white"), cx, cy + 15.0f * s, cx - 3.2f * s, cy - 4.0f * s, cx + 3.2f * s, cy - 4.0f * s);
        addQuad(mesh.get("tr-white"), cx - 16.0f * s, cy + 2.0f * s, cx + 16.0f * s, cy + 2.0f * s, cx + 16.0f * s,
                cy - 1.2f * s, cx - 16.0f * s, cy - 1.2f * s);
        addQuad(mesh.get("tr-white"), cx - 5.0f * s, cy - 6.0f * s, cx + 5.0f * s, cy - 6.0f * s, cx + 5.0f * s,
                cy - 12.0f * s, cx - 5.0f * s, cy - 12.0f * s);

        std::vector<Triangles> batch;
        auto take = [&](const char *name) {
            auto found = mesh.groups.find(name);
            if (found != mesh.groups.end())
            {
                batch.push_back(std::move(found->second));
                mesh.groups.erase(found);
            }
        };
        take("tr-plate");
        take("tr-arc");
        take("tr-minor");
        take("tr-major");
        take("tr-halo");
        take("tr-cyan");
        take("tr-white");
        for (auto &entry : mesh.groups)
        {
            batch.push_back(std::move(entry.second));
        }
        mShader.clearGeometry();
        mShader.setTriangles(batch);
    }

    const float cardPx = std::max(12.0f, 20.0f * mScale);
    const float numPx = std::max(10.0f, 14.0f * mScale);
    const float ringPx = std::max(9.0f, 12.0f * mScale);
    const float relPx = std::max(9.0f, 13.0f * mScale);
    const float metaPx = std::max(10.0f, 16.0f * mScale);
    const float idPx = std::max(12.0f, 22.0f * mScale);
    auto at = [&](float htmlX, float htmlY) {
        return std::pair<float, float>{mCx + (htmlX - 300.0f) * mScale, mCy - (htmlY - 300.0f) * mScale};
    };

    static const int kDeg[12] = {0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330};
    static const char *kTxt[12] = {"N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33"};
    for (int i = 0; i < 12; ++i)
    {
        const float shown = wrap180(static_cast<float>(kDeg[i]) - headingDeg);
        const float ang = radClockwise(shown);
        const float r = 152.0f * mScale;
        const float x = mCx + r * std::sin(ang);
        const float y = mCy + r * std::cos(ang);
        const bool card = (kDeg[i] % 90) == 0;
        paintGlyph(mRose[i], std::string("tr-rose-") + kTxt[i], kTxt[i], card ? cardPx : numPx, card ? kInk : kRoseNum,
                   x, y, -ang);
    }

    static const char *kRingTxt[3] = {"5", "10", "15"};
    static const float kRingX[3] = {253.7f, 214.3f, 175.0f};
    for (int i = 0; i < 3; ++i)
    {
        const auto pos = at(kRingX[i], 304.0f);
        paintGlyph(mRing[i], std::string("tr-ring-") + kRingTxt[i], kRingTxt[i], ringPx, kWhiteText, pos.first,
                   pos.second, 0.0f);
    }

    for (int i = 0; i < 3; ++i)
    {
        mRelOn[i] = placed[i].onScale;
        if (!mRelOn[i])
        {
            continue;
        }
        const int hundreds = static_cast<int>(std::lround(placed[i].relAltFt / 100.0f));
        const int shown = std::clamp(std::abs(hundreds), 0, 99);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%c%02d", hundreds < 0 ? '-' : '+', shown);
        const float lx = placed[i].x > mCx + 20.0f * mScale ? placed[i].x - 22.0f * mScale : placed[i].x + 18.0f * mScale;
        const float ly = placed[i].y + 8.0f * mScale;
        paintGlyph(mRel[i], std::string("tr-rel-") + kTargets[i].id, buf, relPx, kCyanText, lx, ly, 0.0f);
    }

    mSelOn = selected >= 0;
    if (mSelOn)
    {
        const DemoTarget &target = kTargets[selected];
        const Placed &spot = placed[selected];
        char distBuf[16];
        char altBuf[16];
        char gsBuf[16];
        if (spot.distNm >= 10.0f)
        {
            std::snprintf(distBuf, sizeof(distBuf), "%.0fnm", spot.distNm);
        }
        else
        {
            std::snprintf(distBuf, sizeof(distBuf), "%.1fnm", spot.distNm);
        }
        std::snprintf(altBuf, sizeof(altBuf), "%.0fft", target.altFt);
        std::snprintf(gsBuf, sizeof(gsBuf), "%.0fkt", target.gsKt);
        const auto dist = at(230.0f, 392.0f);
        const auto gs = at(370.0f, 392.0f);
        const auto alt = at(230.0f, 418.0f);
        const auto type = at(370.0f, 418.0f);
        const auto ident = at(300.0f, 450.0f);
        paintGlyph(mSelDist, "tr-sel-dist", distBuf, metaPx, kWhiteText, dist.first, dist.second, 0.0f);
        paintGlyph(mSelGs, "tr-sel-gs", gsBuf, metaPx, kWhiteText, gs.first, gs.second, 0.0f);
        paintGlyph(mSelAlt, "tr-sel-alt", altBuf, metaPx, kWhiteText, alt.first, alt.second, 0.0f);
        paintGlyph(mSelType, "tr-sel-type", target.category, metaPx, kWhiteText, type.first, type.second, 0.0f);
        paintGlyph(mSelId, "tr-sel-id", target.id, idPx, kCyanText, ident.first, ident.second, 0.0f);
    }

    const int w = std::max(1, mScreen.getWidth());
    const int h = std::max(1, mScreen.getHeight());
    glm::mat4 mvp(1.0f);
    mvp[0][0] = 2.0f / static_cast<float>(w);
    mvp[1][1] = 2.0f / static_cast<float>(h);
    mvp[3][0] = -1.0f;
    mvp[3][1] = -1.0f;
    mvp[3][2] = -0.2f;
    mShader.setMvpMatrix(mvp);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mShader.render();
    for (int i = 0; i < 12; ++i)
    {
        if (mRose[i].draw)
        {
            mRose[i].draw->render();
        }
    }
    for (int i = 0; i < 3; ++i)
    {
        if (mRing[i].draw)
        {
            mRing[i].draw->render();
        }
        if (mRelOn[i] && mRel[i].draw)
        {
            mRel[i].draw->render();
        }
    }
    if (mSelOn)
    {
        Glyph *selectedGlyphs[] = {&mSelDist, &mSelGs, &mSelAlt, &mSelType, &mSelId};
        for (Glyph *glyph : selectedGlyphs)
        {
            if (glyph->draw)
            {
                glyph->draw->render();
            }
        }
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
