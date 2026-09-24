/// \file route_strip.cpp
/// Draws the route strip under the attitude display.
#include "route_strip.h"
#include "data_manager_sim.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kNmPerMeter = 1.0f / 1852.0f;
constexpr float kKtPerMps = 1.943844f;
/// Wrocław (EPWR), the same active waypoint as the HSI.
constexpr double kWptLat = 51.1027;
constexpr double kWptLon = 16.8858;
constexpr char kFromId[] = "EPMR";
constexpr char kToId[] = "EPWR";

/// drawText packs blue, green, red, alpha from the high byte.
constexpr uint32_t kMagentaText = 0xD02FFFFFu;
constexpr uint32_t kGreenText = 0x6AFF39FFu;
constexpr uint32_t kMuted = 0xD5D5D5FFu;

/// B612 Mono advance is 1300 units of a 2000-unit em.
float textWidth(float px, int chars)
{
    return px * 0.65f * static_cast<float>(std::max(chars, 1));
}

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

std::string clockHm(int totalSec)
{
    const int sec = ((totalSec % 86400) + 86400) % 86400;
    const int h = sec / 3600;
    const int m = (sec % 3600) / 60;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return buf;
}

std::string formatEte(float seconds)
{
    const int sec = std::max(0, static_cast<int>(std::lround(seconds)));
    const int h = sec / 3600;
    const int m = (sec % 3600) / 60;
    const int s = sec % 60;
    char buf[16];
    if (h > 0)
    {
        std::snprintf(buf, sizeof(buf), "%d:%02d", h, m);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }
    return buf;
}
} // namespace

RouteStrip::RouteStrip(Frame &frame, IDataManager &data) : IWidget(frame), mData(data), mShader()
{
}

void RouteStrip::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void RouteStrip::prepareColors()
{
    if (mColorsReady)
    {
        return;
    }
    mShader.setColor("route-plate", 0x06080A75u);
    mShader.setColor("route-arrow", 0xFF2FD0FFu);
    mColorsReady = true;
}

void RouteStrip::paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size,
                            uint32_t color, float x, float y)
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
    glyph.draw->setTransformationMatrix(xform);
}

void RouteStrip::render()
{
    if (!mEnabled)
    {
        return;
    }
    prepareColors();

    const int w = std::max(1, mScreen.getWidth());
    const int h = std::max(1, mScreen.getHeight());
    const float s = static_cast<float>(h) / 600.0f;
    const float identPx = 22.0f * s;
    const float valuePx = 17.0f * s;
    const float labelPx = 12.0f * s;
    const float padX = 14.0f * s;
    const float padY = 8.0f * s;
    const float rowGap = 8.0f * s;
    const float labelGap = 2.0f * s;
    const float colGap = 28.0f * s;
    const float arrowW = 26.0f * s;
    const float identGap = 8.0f * s;

    const LocationData &place = mData.getLocationData();
    const DynamicsData &motion = mData.getDynamicsData();
    const double dNorth = (kWptLat - place.latitude) * 111320.0;
    const double dEast = (kWptLon - place.longitude) * 111320.0 * std::cos(place.latitude * kPi / 180.0);
    const float distNm = static_cast<float>(std::hypot(dNorth, dEast) * kNmPerMeter);
    const float gsKt = motion.airspeed * kKtPerMps;

    char distBuf[16];
    std::snprintf(distBuf, sizeof(distBuf), "%.1fnm", distNm);
    std::string ete = "--:--";
    std::string eta = "--:--";
    if (gsKt >= 5.0f)
    {
        const float eteSec = (distNm / gsKt) * 3600.0f;
        ete = formatEte(eteSec);
        std::time_t now = std::time(nullptr);
        std::tm utc{};
        gmtime_r(&now, &utc);
        const int nowSec = utc.tm_hour * 3600 + utc.tm_min * 60 + utc.tm_sec;
        eta = clockHm(nowSec + static_cast<int>(std::lround(eteSec))) + "utc";
    }

    const float fromW = textWidth(identPx, static_cast<int>(std::char_traits<char>::length(kFromId)));
    const float toW = textWidth(identPx, static_cast<int>(std::char_traits<char>::length(kToId)));
    const float legW = fromW + identGap + arrowW + identGap + toW;

    const float distW = std::max(textWidth(valuePx, static_cast<int>(std::strlen(distBuf))), textWidth(labelPx, 4));
    const float eteW = std::max(textWidth(valuePx, static_cast<int>(ete.size())), textWidth(labelPx, 3));
    const float etaW = std::max(textWidth(valuePx, static_cast<int>(eta.size())), textWidth(labelPx, 3));
    const float statsW = distW + colGap + eteW + colGap + etaW;
    const float plateW = std::max(legW, statsW) + padX * 2.0f;
    const float plateH = padY + identPx + rowGap + valuePx + labelGap + labelPx + padY;

    const float cx = static_cast<float>(w) * 0.5f;
    const float plateBottom = std::max(6.0f, static_cast<float>(h) * 0.012f);
    const float plateTop = plateBottom + plateH;
    const float plateLeft = cx - plateW * 0.5f;
    const float plateRight = cx + plateW * 0.5f;

    const float identY = plateTop - padY - identPx * 0.5f;
    const float valueY = identY - identPx * 0.5f - rowGap - valuePx * 0.5f;
    const float labelY = valueY - valuePx * 0.5f - labelGap - labelPx * 0.5f;

    const float legLeft = cx - legW * 0.5f;
    const float fromX = legLeft + fromW * 0.5f;
    const float arrowX = fromX + fromW * 0.5f + identGap + arrowW * 0.5f;
    const float toX = arrowX + arrowW * 0.5f + identGap + toW * 0.5f;

    const float statsLeft = cx - statsW * 0.5f;
    const float distX = statsLeft + distW * 0.5f;
    const float eteX = distX + distW * 0.5f + colGap + eteW * 0.5f;
    const float etaX = eteX + eteW * 0.5f + colGap + etaW * 0.5f;

    Triangles plate;
    plate.material = "route-plate";
    addQuad(plate, plateLeft, plateBottom, plateRight, plateBottom, plateRight, plateTop, plateLeft, plateTop);

    Triangles arrow;
    arrow.material = "route-arrow";
    const float shaftY = identY;
    const float shaftH = 2.2f * s;
    const float head = 7.0f * s;
    const float shaftRight = arrowX + arrowW * 0.5f - head * 0.55f;
    const float shaftLeft = arrowX - arrowW * 0.5f;
    addQuad(arrow, shaftLeft, shaftY - shaftH, shaftRight, shaftY - shaftH, shaftRight, shaftY + shaftH, shaftLeft,
            shaftY + shaftH);
    const float tipX = arrowX + arrowW * 0.5f;
    addTri(arrow, shaftRight - head * 0.15f, shaftY + head * 0.72f, tipX, shaftY, shaftRight - head * 0.15f,
           shaftY - head * 0.72f);

    mShader.clearGeometry();
    mShader.setTriangles({plate, arrow});

    paintGlyph(mFrom, "route-from", kFromId, identPx, kMagentaText, fromX, identY);
    paintGlyph(mTo, "route-to", kToId, identPx, kMagentaText, toX, identY);
    paintGlyph(mDistVal, "route-dist-val", distBuf, valuePx, kGreenText, distX, valueY);
    paintGlyph(mEteVal, "route-ete-val", ete, valuePx, kGreenText, eteX, valueY);
    paintGlyph(mEtaVal, "route-eta-val", eta, valuePx, kGreenText, etaX, valueY);
    paintGlyph(mDistLbl, "route-dist-lbl", "DIST", labelPx, kMuted, distX, labelY);
    paintGlyph(mEteLbl, "route-ete-lbl", "ETE", labelPx, kMuted, eteX, labelY);
    paintGlyph(mEtaLbl, "route-eta-lbl", "ETA", labelPx, kMuted, etaX, labelY);

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
    Glyph *glyphs[] = {&mFrom, &mTo, &mDistVal, &mEteVal, &mEtaVal, &mDistLbl, &mEteLbl, &mEtaLbl};
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
