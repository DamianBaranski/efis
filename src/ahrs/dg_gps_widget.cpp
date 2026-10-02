/// \file dg_gps_widget.cpp
/// Draws the AV-30 DG GPS page from heading, track, and the armed waypoint.
#include "dg_gps_widget.h"
#include "data_manager_sim.h"
#include "flight_plan.h"
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
/// HTML glass radius. Slot outer maps to this so the lubber fits.
constexpr float kHtmlR = 266.0f;
constexpr float kHtmlCx = 300.0f;
constexpr float kHtmlCy = 300.0f;
/// One-dot CDI in HTML pixels. Full scale is 1.0 nm.
constexpr float kFullPx = 48.0f;
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

/// Airplane silhouette in heading-up local pixels, +y toward the lubber.
/// Copied from the DG GPS HTML path with SVG Y flipped.
constexpr float kPlane[] = {
    0.0f,   16.0f,  1.7f,  3.2f,  16.2f,  0.2f,  16.2f, -3.2f, 2.1f,  -1.5f, 1.5f,  -7.2f,
    5.4f,  -10.2f,  5.4f, -12.6f,  0.0f, -11.0f, -5.4f, -12.6f, -5.4f, -10.2f, -1.5f, -7.2f,
    -2.1f,  -1.5f, -16.2f, -3.2f, -16.2f,  0.2f,  -1.7f,  3.2f,
};
constexpr int kPlaneN = 16;

/// HTML silhouette is sized for a 600 px bezel. The side slot is smaller, so the
/// airplane is drawn 2.6 times that path.
constexpr float kPlaneGain = 2.6f;

void addAircraft(Triangles &dst, float cx, float cy, float s)
{
    const float g = s * kPlaneGain;
    for (int i = 0; i < kPlaneN; ++i)
    {
        const int j = (i + 1) % kPlaneN;
        addTri(dst, cx, cy, cx + kPlane[i * 2] * g, cy + kPlane[i * 2 + 1] * g, cx + kPlane[j * 2] * g,
               cy + kPlane[j * 2 + 1] * g);
    }
}

void addBolt(Triangles &dst, float ox, float oy, float s)
{
    auto pt = [&](float lx, float ly, float &x, float &y) {
        x = ox + lx * 1.15f * s;
        y = oy - ly * 1.15f * s;
    };
    float x0, y0, x1, y1, x2, y2, x3, y3, x4, y4, x5, y5;
    pt(4.0f, -9.0f, x0, y0);
    pt(0.0f, 1.0f, x1, y1);
    pt(3.2f, 1.0f, x2, y2);
    pt(-1.0f, 9.0f, x3, y3);
    pt(6.0f, 0.0f, x4, y4);
    pt(2.4f, 0.0f, x5, y5);
    addTri(dst, x0, y0, x1, y1, x2, y2);
    addTri(dst, x2, y2, x3, y3, x4, y4);
    addTri(dst, x2, y2, x4, y4, x5, y5);
    addTri(dst, x0, y0, x2, y2, x5, y5);
}
} // namespace

DgGpsWidget::DgGpsWidget(Frame &frame, IDataManager &data, Slot slot)
    : IWidget(frame), mData(data), mSlot(slot), mShader()
{
}

void DgGpsWidget::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void DgGpsWidget::prepareColors()
{
    if (mColorsReady)
    {
        return;
    }
    mShader.setColor("dg-plate", 0x06080A75u);
    mShader.setColor("dg-white", 0xF4F4F4FFu);
    mShader.setColor("dg-ink", 0xF4F4F4FFu);
    mShader.setColor("dg-major", 0xD0D0D0FFu);
    mShader.setColor("dg-minor", 0x8A8A8AFFu);
    mShader.setColor("dg-magenta", 0xFF2FD0FFu);
    mShader.setColor("dg-green", 0x39FF6AFFu);
    mColorsReady = true;
}

void DgGpsWidget::layout()
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
    mScale = outer / kHtmlR;
    const bool right = mSlot == Slot::RightBottom || mSlot == Slot::RightTop;
    const bool top = mSlot == Slot::LeftTop || mSlot == Slot::RightTop;
    mCx = right ? static_cast<float>(w) - outer - inset : outer + inset;
    mCy = outer + inset + (top ? outer * 2.0f + inset : 0.0f);
}

void DgGpsWidget::paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size,
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

void DgGpsWidget::rebuildDial(float headingDeg, float courseDeg, float trackDeg, float xteNm)
{
    Buckets mesh;
    const float cx = mCx;
    const float cy = mCy;
    const float s = mScale;
    addDisc(mesh.get("dg-plate"), cx, cy, 255.0f * s, 48);

    for (int deg = 0; deg < 360; deg += 5)
    {
        const char *mat = "dg-minor";
        float inner = 216.0f;
        float width = 1.15f;
        if (deg % 30 == 0)
        {
            mat = "dg-white";
            inner = 196.0f;
            width = 2.4f;
        }
        else if (deg % 10 == 0)
        {
            mat = "dg-major";
            inner = 208.0f;
            width = 1.7f;
        }
        const float ang = radClockwise(static_cast<float>(deg) - headingDeg);
        addTick(mesh.get(mat), cx, cy, ang, inner * s, 224.0f * s, width * s * 0.5f);
    }

    const float relCrs = radClockwise(courseDeg - headingDeg);
    const float xOff = std::clamp(-xteNm * kFullPx, -kFullPx, kFullPx) * s;
    const float halfW = std::max(1.4f, 2.2f * s);
    float n0x, n0y, n1x, n1y, n2x, n2y, n3x, n3y;
    spin(cx, cy, xOff - halfW, 150.0f * s, relCrs, n0x, n0y);
    spin(cx, cy, xOff + halfW, 150.0f * s, relCrs, n1x, n1y);
    spin(cx, cy, xOff + halfW, -92.0f * s, relCrs, n2x, n2y);
    spin(cx, cy, xOff - halfW, -92.0f * s, relCrs, n3x, n3y);
    addQuad(mesh.get("dg-magenta"), n0x, n0y, n1x, n1y, n2x, n2y, n3x, n3y);

    float ax0, ay0, ax1, ay1, ax2, ay2;
    spin(cx, cy, xOff, 182.0f * s, relCrs, ax0, ay0);
    spin(cx, cy, xOff + 11.0f * s, 154.0f * s, relCrs, ax1, ay1);
    spin(cx, cy, xOff - 11.0f * s, 154.0f * s, relCrs, ax2, ay2);
    addTri(mesh.get("dg-magenta"), ax0, ay0, ax1, ay1, ax2, ay2);

    const float relTrk = radClockwise(trackDeg - headingDeg);
    float tx0, ty0, tx1, ty1, tx2, ty2;
    spin(cx, cy, 0.0f, 234.0f * s, relTrk, tx0, ty0);
    spin(cx, cy, 10.0f * s, 216.0f * s, relTrk, tx1, ty1);
    spin(cx, cy, -10.0f * s, 216.0f * s, relTrk, tx2, ty2);
    addTri(mesh.get("dg-green"), tx0, ty0, tx1, ty1, tx2, ty2);

    float lx0, ly0, lx1, ly1, lx2, ly2, lx3, ly3;
    spin(cx, cy, -1.5f * s, 262.0f * s, 0.0f, lx0, ly0);
    spin(cx, cy, 1.5f * s, 262.0f * s, 0.0f, lx1, ly1);
    spin(cx, cy, 1.5f * s, 246.0f * s, 0.0f, lx2, ly2);
    spin(cx, cy, -1.5f * s, 246.0f * s, 0.0f, lx3, ly3);
    addQuad(mesh.get("dg-ink"), lx0, ly0, lx1, ly1, lx2, ly2, lx3, ly3);

    addAircraft(mesh.get("dg-ink"), cx, cy, s);

    const float boltX = cx + (394.0f - kHtmlCx) * s;
    const float boltY = cy - (528.0f - kHtmlCy) * s;
    addBolt(mesh.get("dg-ink"), boltX, boltY, s * 1.6f);

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
    take("dg-plate");
    take("dg-minor");
    take("dg-major");
    take("dg-white");
    take("dg-magenta");
    take("dg-green");
    take("dg-ink");
    for (auto &entry : mesh.groups)
    {
        batch.push_back(std::move(entry.second));
    }
    mShader.clearGeometry();
    mShader.setTriangles(batch);
}

void DgGpsWidget::render()
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
    const float trackDeg = headingDeg;

    double wptLat = kWptLat;
    double wptLon = kWptLon;
    std::string wptId = kWptId;
    double homeLat = DataManagerSim::kEpmrLatitude;
    double homeLon = DataManagerSim::kEpmrLongitude;
    if (mPlan != nullptr && mPlan->armed())
    {
        wptLat = mPlan->armedToLat();
        wptLon = mPlan->armedToLon();
        wptId = mPlan->armedToIdent();
        homeLat = mPlan->armedFromLat();
        homeLon = mPlan->armedFromLon();
    }

    const double dNorth = (wptLat - place.latitude) * 111320.0;
    const double dEast = (wptLon - place.longitude) * 111320.0 * std::cos(place.latitude * kPi / 180.0);
    const double distM = std::hypot(dNorth, dEast);
    const float bearingDeg = wrap360(static_cast<float>(std::atan2(dEast, dNorth) * 180.0 / kPi));

    const double courseEast = (wptLon - homeLon) * 111320.0 * std::cos(homeLat * kPi / 180.0);
    const double courseNorth = (wptLat - homeLat) * 111320.0;
    const double course = std::atan2(courseEast, courseNorth);
    const float courseDeg = wrap360(static_cast<float>(course * 180.0 / kPi));
    const double relNorth = (place.latitude - homeLat) * 111320.0;
    const double relEast = (place.longitude - homeLon) * 111320.0 * std::cos(homeLat * kPi / 180.0);
    const double cross = -relNorth * std::sin(course) + relEast * std::cos(course);
    const float xteNm = static_cast<float>(cross * kNmPerMeter);

    rebuildDial(headingDeg, courseDeg, trackDeg, xteNm);

    const float labelPx = std::max(10.0f, 12.0f * mScale);
    const float valuePx = std::max(14.0f, 26.0f * mScale);
    const float cardPx = std::max(12.0f, 24.0f * mScale);
    const float numPx = std::max(10.0f, 16.0f * mScale);
    const float chromePx = std::max(10.0f, 15.0f * mScale);
    auto at = [&](float htmlX, float htmlY) {
        return std::pair<float, float>{mCx + (htmlX - kHtmlCx) * mScale, mCy - (htmlY - kHtmlCy) * mScale};
    };

    static const int kDeg[12] = {0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330};
    static const char *kTxt[12] = {"N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33"};
    for (int i = 0; i < 12; ++i)
    {
        const float shown = static_cast<float>(kDeg[i]) - headingDeg;
        const float ang = radClockwise(shown);
        const float r = 184.0f * mScale;
        const float x = mCx + r * std::sin(ang);
        const float y = mCy + r * std::cos(ang);
        const bool card = (kDeg[i] % 90) == 0;
        paintGlyph(mRose[i], std::string("dg-rose-") + kTxt[i], kTxt[i], card ? cardPx : numPx, card ? kInk : 0xECECECFF,
                   x, y, -ang);
    }

    char distBuf[16];
    char brgBuf[8];
    char xteBuf[16];
    std::snprintf(distBuf, sizeof(distBuf), "%.1f", distM * kNmPerMeter);
    std::snprintf(brgBuf, sizeof(brgBuf), "%03d", static_cast<int>(std::lround(bearingDeg)) % 360);
    const float xteShown = std::round(std::fabs(xteNm) * 10.0f) / 10.0f;
    std::snprintf(xteBuf, sizeof(xteBuf), "%.1f", xteShown);

    const auto wpt = at(242.0f, 188.0f);
    const auto wptV = at(242.0f, 216.0f);
    const auto brg = at(358.0f, 188.0f);
    const auto brgV = at(358.0f, 216.0f);
    const auto dist = at(242.0f, 384.0f);
    const auto distV = at(242.0f, 412.0f);
    const auto xte = at(358.0f, 384.0f);
    const auto xteV = at(358.0f, 412.0f);
    const auto page = at(300.0f, 540.0f);
    const auto bat = at(360.0f, 540.0f);
    paintGlyph(mWptLbl, "dg-wpt-lbl", "WPT", labelPx, kMuted, wpt.first, wpt.second, 0.0f);
    paintGlyph(mWptVal, std::string("dg-wpt-val-") + wptId, wptId, valuePx, kGreenText, wptV.first, wptV.second, 0.0f);
    paintGlyph(mBrgLbl, "dg-brg-lbl", "BRG", labelPx, kMuted, brg.first, brg.second, 0.0f);
    paintGlyph(mBrgVal, "dg-brg-val", brgBuf, valuePx, kGreenText, brgV.first, brgV.second, 0.0f);
    paintGlyph(mDistLbl, "dg-dist-lbl", "DIST", labelPx, kMuted, dist.first, dist.second, 0.0f);
    paintGlyph(mDistVal, "dg-dist-val", distBuf, valuePx, kGreenText, distV.first, distV.second, 0.0f);
    paintGlyph(mXteLbl, "dg-xte-lbl", "XTE", labelPx, kMuted, xte.first, xte.second, 0.0f);
    paintGlyph(mXteVal, "dg-xte-val", xteBuf, valuePx, kGreenText, xteV.first, xteV.second, 0.0f);
    paintGlyph(mPage, "dg-page", "2:3", chromePx, kMuted, page.first, page.second, 0.0f);
    paintGlyph(mBat, "dg-bat", "100%", chromePx, kMuted, bat.first, bat.second, 0.0f);

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
    Glyph *glyphs[] = {&mWptLbl, &mWptVal, &mBrgLbl, &mBrgVal, &mDistLbl, &mDistVal,
                       &mXteLbl, &mXteVal, &mPage,   &mBat};
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
