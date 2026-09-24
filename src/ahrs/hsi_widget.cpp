/// \file hsi_widget.cpp
/// Draws the bottom-right HSI from heading, position, and a fixed waypoint.
#include "hsi_widget.h"
#include "data_manager_sim.h"
#include "geo_coord_utils.h"
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
constexpr float kNmPerMeter = 1.0f / 1852.0f;
/// Wrocław (EPWR), the active waypoint for the overlay.
constexpr double kWptLat = 51.1027;
constexpr double kWptLon = 16.8858;
constexpr char kWptId[] = "EPWR";

/// drawText packs blue, green, red, alpha from the high byte.
constexpr uint32_t kInk = 0xFFFFFFFFu;
constexpr uint32_t kMuted = 0xD5D5D5FFu;
constexpr uint32_t kGreenText = 0x6AFF39FFu;

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
        addTri(dst, cx, cy, cx + radius * std::cos(a0), cy + radius * std::sin(a0),
               cx + radius * std::cos(a1), cy + radius * std::sin(a1));
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

float radClockwise(float degFromUp)
{
    return degFromUp * kPi / 180.0f;
}
} // namespace

HsiWidget::HsiWidget(Frame &frame, IDataManager &data, Slot slot) : IWidget(frame), mData(data), mSlot(slot), mShader()
{
}

void HsiWidget::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void HsiWidget::prepareColors()
{
    if (mColorsReady)
    {
        return;
    }
    mShader.setColor("hsi-plate", 0x06080A75u);
    mShader.setColor("hsi-hub", 0x06080A75u);
    mShader.setColor("hsi-white", 0xF4F4F4FFu);
    mShader.setColor("hsi-ink", 0xF4F4F4FFu);
    mShader.setColor("hsi-major", 0xD0D0D0FFu);
    mShader.setColor("hsi-minor", 0x7D7D7DFFu);
    mShader.setColor("hsi-magenta", 0xFF2FD0FFu);
    mShader.setColor("hsi-green", 0x39FF6AFFu);
    mColorsReady = true;
}

void HsiWidget::layout()
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
    // 200 dial units reaches the lubber tip. Same diameter as the single corner dial.
    const float outer = std::max(36.0f, column * 0.5f - inset);
    mScale = outer / 200.0f;
    const bool right = mSlot == Slot::RightBottom || mSlot == Slot::RightTop;
    const bool top = mSlot == Slot::LeftTop || mSlot == Slot::RightTop;
    mCx = right ? static_cast<float>(w) - outer - inset : outer + inset;
    mCy = outer + inset + (top ? outer * 2.0f + inset : 0.0f);
}

void HsiWidget::paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size,
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

void HsiWidget::rebuildDial(float headingDeg, float bearingDeg, float xteNm)
{
    Buckets mesh;
    const float cx = mCx;
    const float cy = mCy;
    const float s = mScale;
    addDisc(mesh.get("hsi-plate"), cx, cy, 168.0f * s, 48);

    for (int deg = 0; deg < 360; deg += 5)
    {
        const char *mat = "hsi-minor";
        float inner = 180.0f;
        float width = 1.1f;
        if (deg % 30 == 0)
        {
            mat = "hsi-white";
            inner = 164.0f;
            width = 2.0f;
        }
        else if (deg % 10 == 0)
        {
            mat = "hsi-major";
            inner = 172.0f;
            width = 1.6f;
        }
        const float ang = radClockwise(static_cast<float>(deg) - headingDeg);
        addTick(mesh.get(mat), cx, cy, ang, inner * s, 188.0f * s, width * s * 0.5f);
    }

    for (int i = 1; i <= 3; ++i)
    {
        const float o = static_cast<float>(i) * 24.0f * s;
        const float half = 8.0f * s;
        addQuad(mesh.get("hsi-white"), cx + o - s, cy - half, cx + o + s, cy - half, cx + o + s, cy + half, cx + o - s,
                cy + half);
        addQuad(mesh.get("hsi-white"), cx - o - s, cy - half, cx - o + s, cy - half, cx - o + s, cy + half, cx - o - s,
                cy + half);
        addQuad(mesh.get("hsi-white"), cx - half, cy + o - s, cx + half, cy + o - s, cx + half, cy + o + s, cx - half,
                cy + o + s);
        addQuad(mesh.get("hsi-white"), cx - half, cy - o - s, cx + half, cy - o - s, cx + half, cy - o + s, cx - half,
                cy - o + s);
    }

    const float xOff = std::clamp(xteNm * 220.0f, -112.0f, 112.0f) * s;
    addQuad(mesh.get("hsi-magenta"), cx + xOff - 2.2f * s, cy + 86.0f * s, cx + xOff + 2.2f * s, cy + 86.0f * s,
            cx + xOff + 2.2f * s, cy - 104.0f * s, cx + xOff - 2.2f * s, cy - 104.0f * s);
    addQuad(mesh.get("hsi-magenta"), cx - 124.0f * s, cy - 2.2f * s, cx + 124.0f * s, cy - 2.2f * s, cx + 124.0f * s,
            cy + 2.2f * s, cx - 124.0f * s, cy + 2.2f * s);

    addDisc(mesh.get("hsi-hub"), cx, cy, 13.0f * s, 20);
    addAnnulus(mesh.get("hsi-ink"), cx, cy, 11.5f * s, 14.0f * s, 24);

    const float relBrg = radClockwise(bearingDeg - headingDeg);
    float bx0, by0, bx1, by1, bx2, by2;
    spin(cx, cy, 0.0f, 182.0f * s, relBrg, bx0, by0);
    spin(cx, cy, -7.0f * s, 166.0f * s, relBrg, bx1, by1);
    spin(cx, cy, 7.0f * s, 166.0f * s, relBrg, bx2, by2);
    addTri(mesh.get("hsi-magenta"), bx0, by0, bx1, by1, bx2, by2);

    float lx0, ly0, lx1, ly1, lx2, ly2;
    spin(cx, cy, 0.0f, 200.0f * s, 0.0f, lx0, ly0);
    spin(cx, cy, -7.0f * s, 186.0f * s, 0.0f, lx1, ly1);
    spin(cx, cy, 7.0f * s, 186.0f * s, 0.0f, lx2, ly2);
    addTri(mesh.get("hsi-ink"), lx0, ly0, lx1, ly1, lx2, ly2);

    std::vector<Triangles> batch;
    batch.reserve(mesh.groups.size());
    auto take = [&](const char *name) {
        auto found = mesh.groups.find(name);
        if (found != mesh.groups.end())
        {
            batch.push_back(std::move(found->second));
            mesh.groups.erase(found);
        }
    };
    take("hsi-plate");
    take("hsi-minor");
    take("hsi-major");
    take("hsi-white");
    take("hsi-magenta");
    take("hsi-hub");
    take("hsi-ink");
    take("hsi-green");
    for (auto &entry : mesh.groups)
    {
        batch.push_back(std::move(entry.second));
    }
    mShader.clearGeometry();
    mShader.setTriangles(batch);
}

void HsiWidget::render()
{
    if (!mEnabled)
    {
        return;
    }
    layout();
    prepareColors();

    const AttitudeData &attitude = mData.getAttitudeData();
    const LocationData &place = mData.getLocationData();
    const float headingDeg = wrap360(attitude.heading * 180.0f / kPi);

    const double dNorth = (kWptLat - place.latitude) * 111320.0;
    const double dEast = (kWptLon - place.longitude) * 111320.0 * std::cos(place.latitude * kPi / 180.0);
    const double distM = std::hypot(dNorth, dEast);
    const float bearingDeg = wrap360(static_cast<float>(std::atan2(dEast, dNorth) * 180.0 / kPi));

    const double homeLat = DataManagerSim::kEpmrLatitude;
    const double homeLon = DataManagerSim::kEpmrLongitude;
    const double courseEast = (kWptLon - homeLon) * 111320.0 * std::cos(homeLat * kPi / 180.0);
    const double courseNorth = (kWptLat - homeLat) * 111320.0;
    const double course = std::atan2(courseEast, courseNorth);
    const double relNorth = (place.latitude - homeLat) * 111320.0;
    const double relEast = (place.longitude - homeLon) * 111320.0 * std::cos(homeLat * kPi / 180.0);
    const double cross = -relNorth * std::sin(course) + relEast * std::cos(course);
    const float xteNm = static_cast<float>(cross * kNmPerMeter);

    rebuildDial(headingDeg, bearingDeg, xteNm);

    const float labelPx = std::max(10.0f, 12.0f * mScale);
    const float valuePx = std::max(14.0f, 22.0f * mScale);
    const float cardPx = std::max(12.0f, 20.0f * mScale);
    const float numPx = std::max(10.0f, 14.0f * mScale);
    auto at = [&](float htmlX, float htmlY) {
        return std::pair<float, float>{mCx + (htmlX - 300.0f) * mScale, mCy - (htmlY - 300.0f) * mScale};
    };

    static const int kDeg[12] = {0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330};
    static const char *kTxt[12] = {"N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33"};
    for (int i = 0; i < 12; ++i)
    {
        const float shown = static_cast<float>(kDeg[i]) - headingDeg;
        const float ang = radClockwise(shown);
        const float r = 152.0f * mScale;
        const float x = mCx + r * std::sin(ang);
        const float y = mCy + r * std::cos(ang);
        const bool card = (kDeg[i] % 90) == 0;
        paintGlyph(mRose[i], std::string("hsi-rose-") + kTxt[i], kTxt[i], card ? cardPx : numPx, card ? kInk : 0xECECECFF,
                   x, y, -ang);
    }

    char distBuf[16];
    char brgBuf[8];
    char xteBuf[16];
    std::snprintf(distBuf, sizeof(distBuf), "%.1f", distM * kNmPerMeter);
    std::snprintf(brgBuf, sizeof(brgBuf), "%03d", static_cast<int>(std::lround(bearingDeg)) % 360);
    const float xteShown = std::round(xteNm * 10.0f) / 10.0f;
    std::snprintf(xteBuf, sizeof(xteBuf), "%.1f", xteShown);

    const auto wpt = at(242.0f, 188.0f);
    const auto wptV = at(242.0f, 216.0f);
    const auto brg = at(358.0f, 188.0f);
    const auto brgV = at(358.0f, 216.0f);
    const auto dist = at(242.0f, 384.0f);
    const auto distV = at(242.0f, 412.0f);
    const auto xte = at(358.0f, 384.0f);
    const auto xteV = at(358.0f, 412.0f);
    paintGlyph(mWptLbl, "hsi-wpt-lbl", "WPT", labelPx, kMuted, wpt.first, wpt.second, 0.0f);
    paintGlyph(mWptVal, "hsi-wpt-val", kWptId, valuePx, kGreenText, wptV.first, wptV.second, 0.0f);
    paintGlyph(mBrgLbl, "hsi-brg-lbl", "BRG", labelPx, kMuted, brg.first, brg.second, 0.0f);
    paintGlyph(mBrgVal, "hsi-brg-val", brgBuf, valuePx, kGreenText, brgV.first, brgV.second, 0.0f);
    paintGlyph(mDistLbl, "hsi-dist-lbl", "DIST", labelPx, kMuted, dist.first, dist.second, 0.0f);
    paintGlyph(mDistVal, "hsi-dist-val", distBuf, valuePx, kGreenText, distV.first, distV.second, 0.0f);
    paintGlyph(mXteLbl, "hsi-xte-lbl", "XTE", labelPx, kMuted, xte.first, xte.second, 0.0f);
    paintGlyph(mXteVal, "hsi-xte-val", xteBuf, valuePx, kGreenText, xteV.first, xteV.second, 0.0f);

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
    Glyph *glyphs[] = {&mWptLbl, &mWptVal, &mBrgLbl, &mBrgVal, &mDistLbl, &mDistVal, &mXteLbl, &mXteVal};
    for (int i = 0; i < 12; ++i)
    {
        if (mRose[i].draw)
        {
            mRose[i].draw->render();
        }
    }
    for (Glyph *glyph : glyphs)
    {
        if (glyph->draw)
        {
            glyph->draw->render();
        }
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
