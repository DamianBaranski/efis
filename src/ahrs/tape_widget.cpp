/// \file tape_widget.cpp
/// Airspeed and altitude tapes. Arrows share the aircraft-symbol center line.
#include "tape_widget.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

namespace
{
constexpr float kKtPerMps = 1.94384449f;
constexpr float kFtPerM = 3.2808399f;
constexpr float kFpmPerMps = 196.850394f;
constexpr float kSymbolHalf = 170.0f;
constexpr float kVso = 40.0f;
constexpr float kVs = 48.0f;
constexpr float kVfe = 85.0f;
constexpr float kVno = 129.0f;
constexpr float kVne = 163.0f;

constexpr uint32_t kInk = 0xF4F4F4FFu;
constexpr uint32_t kMuted = 0xC8C8C8FFu;
constexpr uint32_t kMagenta = 0xD02FFFFFu;

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

void addRect(Triangles &dst, float x, float y, float w, float h)
{
    if (w < 0.5f || h < 0.5f)
    {
        return;
    }
    addQuad(dst, x, y, x + w, y, x + w, y + h, x, y + h);
}

/// Vertical strip clipped to [clip0, clip1]. y0 and y1 are the unclipped ends.
void addVBand(Triangles &dst, float x, float w, float y0, float y1, float clip0, float clip1)
{
    const float lo = std::max(clip0, std::min(y0, y1));
    const float hi = std::min(clip1, std::max(y0, y1));
    addRect(dst, x, lo, w, hi - lo);
}

float textWidth(const std::string &text, float size)
{
    return static_cast<float>(text.size()) * size * 0.60f;
}

std::string whole(float value)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
    return buf;
}

std::string vsText(float fpm)
{
    const int v = static_cast<int>(std::lround(fpm / 10.0f)) * 10;
    if (v == 0)
    {
        return "0";
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%+d", v);
    return buf;
}
} // namespace

TapeWidget::TapeWidget(Frame &frame, IDataManager &data) : IWidget(frame), mData(data), mShader() {}

void TapeWidget::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void TapeWidget::prepareColors()
{
    if (mColorsReady)
    {
        return;
    }
    mShader.setColor("tp-plate", 0x06080A75u);
    mShader.setColor("tp-box", 0x07090CFFu);
    mShader.setColor("tp-white", 0xF4F4F4FFu);
    mShader.setColor("tp-minor", 0x9A9A9AFFu);
    mShader.setColor("tp-flaps", 0xC8C2B6FFu);
    mShader.setColor("tp-green", 0x1F8C46FFu);
    mShader.setColor("tp-orange", 0xE07A22FFu);
    mShader.setColor("tp-red", 0xFF3B30FFu);
    mColorsReady = true;
}

void TapeWidget::paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size,
                            uint32_t color, float x, float y)
{
    if (text.empty())
    {
        return;
    }
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

void TapeWidget::render()
{
    if (!mEnabled)
    {
        return;
    }
    prepareColors();

    const int w = std::max(1, mScreen.getWidth());
    const int h = std::max(1, mScreen.getHeight());
    const float wf = static_cast<float>(w);
    const float hf = static_cast<float>(h);
    const float design = hf / 600.0f;
    const float symbolScale = std::max(design, wf / 2048.0f);
    const float cx = wf * 0.5f;
    const float cy = hf * 0.5f;
    const float symbolHalf = kSymbolHalf * symbolScale;

    const float column = std::min(hf / 3.0f, wf * 0.28f);
    const float inset = std::max(4.0f, column * 0.04f);
    const float outer = std::max(36.0f, column * 0.5f - inset);
    const float dialInner = outer + inset + outer;
    const float haveL = (cx - symbolHalf) - dialInner;
    const float haveR = (wf - dialInner) - (cx + symbolHalf);

    const float wantL = 138.0f * symbolScale;
    const float wantR = 148.0f * symbolScale;
    float fit = 1.0f;
    if (haveL > 48.0f)
    {
        fit = std::min(fit, haveL / wantL);
    }
    if (haveR > 48.0f)
    {
        fit = std::min(fit, haveR / wantR);
    }
    const float s = symbolScale * std::clamp(fit, 0.55f, 1.15f);

    const float gap = 4.0f * s;
    const float spdTrap = 24.0f * s;
    const float spdTapeW = 110.0f * s;
    const float altTrap = 18.0f * s;
    const float altTapeW = 64.0f * s;
    const float boxH = 44.0f * s;
    const float tapeH = std::min(360.0f * s, hf * 0.62f);
    const float tapeTop = cy + tapeH * 0.5f;
    const float tapeBot = cy - tapeH * 0.5f;
    const float longExtra = 20.0f * s;

    const float spdShort = cx - symbolHalf - gap;
    const float spdTapeR = spdShort - spdTrap;
    const float spdTapeL = spdTapeR - spdTapeW;
    const float spdBoxR = spdTapeR - 2.0f * s;
    const float spdBoxL = spdTapeL + 26.0f * s;
    const float spdBoxW = std::max(48.0f * s, spdBoxR - spdBoxL);

    const float altShort = cx + symbolHalf + gap;
    const float altTapeL = altShort + altTrap;
    const float altTapeR = altTapeL + altTapeW;
    const float altBoxL = altTapeL + 2.0f * s;
    const float altBoxW = 56.0f * s;

    const float barW = 6.0f * s;
    const float barL = altTapeR + 2.0f * s;
    const float barR = barL + barW;
    const float vsBoxL = barR + 10.0f * s;
    const float vsBoxW = 44.0f * s;
    const float vsiH = std::min(tapeH * 0.46f, 168.0f * s);
    const float vsiTop = cy + vsiH * 0.5f;
    const float vsiBot = cy - vsiH * 0.5f;

    const DynamicsData &dynamics = mData.getDynamicsData();
    const LocationData &place = mData.getLocationData();
    const float ias = std::max(0.0f, dynamics.airspeed * kKtPerMps);
    const float altFt = place.altitude * kFtPerM;
    const float vsFpm = dynamics.vertical_speed * kFpmPerMps;
    const float tas = ias * 131.0f / 120.0f;
    const float gs = ias * 121.0f / 120.0f;

    const float spdPx = tapeH / 120.0f;
    const float altPx = tapeH / 1100.0f;
    const float vsTravel = std::max(8.0f, vsiH * 0.5f - boxH * 0.5f);
    const float vsPx = vsTravel / 1500.0f;
    const float vsTip = std::clamp(cy + vsFpm * vsPx, vsiBot + boxH * 0.5f, vsiTop - boxH * 0.5f);

    auto speedY = [&](float kt) { return cy + (kt - ias) * spdPx; };
    auto altY = [&](float ft) { return cy + (ft - altFt) * altPx; };

    Buckets mesh;
    addRect(mesh.get("tp-plate"), spdTapeL, tapeBot, spdTapeW, tapeH);
    addQuad(mesh.get("tp-plate"), spdTapeR, cy + boxH * 0.5f + longExtra, spdShort, cy + boxH * 0.5f, spdShort,
            cy - boxH * 0.5f, spdTapeR, cy - boxH * 0.5f - longExtra);
    addRect(mesh.get("tp-plate"), altTapeL, tapeBot, altTapeW, tapeH);
    addQuad(mesh.get("tp-plate"), altTapeL, cy + boxH * 0.5f + longExtra, altShort, cy + boxH * 0.5f, altShort,
            cy - boxH * 0.5f, altTapeL, cy - boxH * 0.5f - longExtra);
    addRect(mesh.get("tp-plate"), barL, vsiBot, barW + 8.0f * s, vsiH);

    const float footerH = 46.0f * s;
    const float footerGap = 6.0f * s;
    addRect(mesh.get("tp-plate"), spdTapeL, tapeBot - footerGap - footerH, spdTapeW, footerH);
    const float altGroupR = vsBoxL + vsBoxW;
    addRect(mesh.get("tp-plate"), altTapeL, tapeBot - footerGap - 28.0f * s, altGroupR - altTapeL, 28.0f * s);

    const float headH = 30.0f * s;
    const float headGap = 4.0f * s;
    addRect(mesh.get("tp-plate"), spdTapeL, tapeTop + headGap, spdTapeW, headH);
    addRect(mesh.get("tp-plate"), altTapeL, tapeTop + headGap, altGroupR - altTapeL, headH);

    // Dark warm white is the flap range. Dark green is flaps-up. Orange runs to the red Vne line.
    const float arcW = 8.0f * s;
    addVBand(mesh.get("tp-flaps"), spdTapeL + 3.0f * s, arcW, speedY(kVso), speedY(kVfe), tapeBot, tapeTop);
    addVBand(mesh.get("tp-green"), spdTapeL + 13.0f * s, arcW, speedY(kVs), speedY(kVno), tapeBot, tapeTop);
    addVBand(mesh.get("tp-orange"), spdTapeL + 13.0f * s, arcW, speedY(kVno), speedY(kVne), tapeBot, tapeTop);
    const float vneY = speedY(kVne);
    if (vneY >= tapeBot && vneY <= tapeTop)
    {
        addRect(mesh.get("tp-red"), spdTapeL + 3.0f * s, vneY - 2.0f * s, 18.0f * s, 4.0f * s);
    }

    const int spdLo = static_cast<int>(std::floor((ias - tapeH * 0.5f / spdPx) / 5.0f)) * 5;
    const int spdHi = static_cast<int>(std::ceil((ias + tapeH * 0.5f / spdPx) / 5.0f)) * 5;
    for (int kt = std::max(0, spdLo); kt <= spdHi; kt += 5)
    {
        const float y = speedY(static_cast<float>(kt));
        if (y < tapeBot || y > tapeTop)
        {
            continue;
        }
        const bool major = kt % 10 == 0;
        const float x1 = spdTapeR - 6.0f * s;
        const float x0 = major ? x1 - 16.0f * s : x1 - 8.0f * s;
        addRect(major ? mesh.get("tp-white") : mesh.get("tp-minor"), x0, y - (major ? 1.0f : 0.6f) * s, x1 - x0,
                (major ? 2.0f : 1.2f) * s);
    }

    const int altLo = static_cast<int>(std::floor((altFt - tapeH * 0.5f / altPx) / 100.0f)) * 100;
    const int altHi = static_cast<int>(std::ceil((altFt + tapeH * 0.5f / altPx) / 100.0f)) * 100;
    for (int ft = altLo; ft <= altHi; ft += 100)
    {
        const float y = altY(static_cast<float>(ft));
        if (y < tapeBot || y > tapeTop)
        {
            continue;
        }
        const bool major = ft % 200 == 0;
        const float x0 = altTapeL + 6.0f * s;
        const float len = major ? 16.0f * s : 8.0f * s;
        addRect(major ? mesh.get("tp-white") : mesh.get("tp-minor"), x0, y - (major ? 1.0f : 0.6f) * s, len,
                (major ? 2.0f : 1.2f) * s);
    }

    for (int fpm = -1500; fpm <= 1500; fpm += 500)
    {
        const float y = cy + static_cast<float>(fpm) * vsPx;
        if (y < vsiBot || y > vsiTop)
        {
            continue;
        }
        const bool major = fpm % 1000 == 0;
        addRect(mesh.get("tp-white"), barR, y - 0.6f * s, (major ? 6.0f : 3.5f) * s, 1.2f * s);
    }
    const float barTop = std::max(cy, vsTip);
    const float barBot = std::min(cy, vsTip);
    addRect(mesh.get("tp-white"), barL, barBot, barW, std::max(1.2f * s, barTop - barBot));

    addRect(mesh.get("tp-box"), spdBoxL, cy - boxH * 0.5f, spdBoxW, boxH);
    addRect(mesh.get("tp-box"), altBoxL, cy - boxH * 0.5f, altBoxW, boxH);
    addRect(mesh.get("tp-box"), vsBoxL, vsTip - boxH * 0.5f, vsBoxW, boxH);

    const float tipHalf = 12.0f * s;
    addTri(mesh.get("tp-white"), spdBoxR, cy, spdBoxR + 16.0f * s, cy + tipHalf, spdBoxR + 16.0f * s, cy - tipHalf);
    addTri(mesh.get("tp-white"), altBoxL, cy, altBoxL - 16.0f * s, cy + tipHalf, altBoxL - 16.0f * s, cy - tipHalf);
    addTri(mesh.get("tp-white"), barR, vsTip, vsBoxL, vsTip + tipHalf, vsBoxL, vsTip - tipHalf);

    std::vector<Triangles> batch;
    auto take = [&](const char *name) {
        auto found = mesh.groups.find(name);
        if (found != mesh.groups.end())
        {
            batch.push_back(std::move(found->second));
            mesh.groups.erase(found);
        }
    };
    take("tp-plate");
    take("tp-flaps");
    take("tp-green");
    take("tp-orange");
    take("tp-white");
    take("tp-red");
    take("tp-minor");
    take("tp-box");
    mShader.clearGeometry();
    mShader.setTriangles(batch);

    glm::mat4 mvp(1.0f);
    mvp[0][0] = 2.0f / wf;
    mvp[1][1] = 2.0f / hf;
    mvp[3][0] = -1.0f;
    mvp[3][1] = -1.0f;
    mvp[3][2] = -0.2f;
    mShader.setMvpMatrix(mvp);

    const float iasPx = std::max(14.0f, 26.0f * s);
    const float altPxText = std::max(12.0f, std::min(20.0f * s, altBoxW * 0.30f));
    const float vsPxText = std::max(11.0f, std::min(16.0f * s, vsBoxW * 0.32f));
    const float numPx = std::max(10.0f, 15.0f * s);
    const float altNumPx = std::max(9.0f, std::min(numPx, (altTapeW - 24.0f * s) / 3.0f));
    const float footPx = std::max(10.0f, 14.0f * s);
    const std::string iasStr = whole(ias);
    const std::string altStr = whole(altFt);
    const std::string spdTop = iasStr + "KT";
    const std::string altTop = altStr + "FT";
    const std::string vsStr = vsText(vsFpm);
    const std::string tasStr = whole(tas);
    const std::string gsStr = whole(gs);
    const std::string baroStr = "29.92IN";

    const float headPx = std::max(11.0f, 16.0f * s);
    const float headY = tapeTop + headGap + headH * 0.5f;
    paintGlyph(mSpdTop, "tp-spd-top", spdTop, headPx, kInk, spdTapeL + spdTapeW * 0.5f, headY);
    paintGlyph(mAltTop, "tp-alt-top", altTop, headPx, kMagenta, altTapeL + (altGroupR - altTapeL) * 0.5f, headY);

    paintGlyph(mIas, "tp-ias", iasStr, iasPx, kInk, spdBoxL + spdBoxW * 0.5f, cy);
    paintGlyph(mAlt, "tp-alt", altStr, altPxText, kInk, altBoxL + altBoxW * 0.5f, cy);
    paintGlyph(mVs, "tp-vs", vsStr, vsPxText, kInk, vsBoxL + vsBoxW * 0.5f, vsTip);

    const float footTop = tapeBot - footerGap;
    const float tasY = footTop - footerH * 0.28f;
    const float gsY = footTop - footerH * 0.72f;
    paintGlyph(mTasLbl, "tp-tas-l", "TAS", footPx, kMuted, spdTapeL + 8.0f * s + textWidth("TAS", footPx) * 0.5f, tasY);
    paintGlyph(mGsLbl, "tp-gs-l", "GS", footPx, kMuted, spdTapeL + 8.0f * s + textWidth("GS", footPx) * 0.5f, gsY);
    paintGlyph(mTasVal, "tp-tas-v", tasStr, footPx, kInk, spdTapeR - 8.0f * s - textWidth(tasStr, footPx) * 0.5f, tasY);
    paintGlyph(mGsVal, "tp-gs-v", gsStr, footPx, kInk, spdTapeR - 8.0f * s - textWidth(gsStr, footPx) * 0.5f, gsY);

    const float baroY = tapeBot - footerGap - 14.0f * s;
    paintGlyph(mBaroLbl, "tp-baro-l", "BARO", footPx, kMuted, altTapeL + 6.0f * s + textWidth("BARO", footPx) * 0.5f,
               baroY);
    paintGlyph(mBaroVal, "tp-baro-v", baroStr, footPx, kInk, altGroupR - 8.0f * s - textWidth(baroStr, footPx) * 0.5f,
               baroY);

    for (int i = 0; i < kNums; ++i)
    {
        mSpdUse[i] = false;
        mAltUse[i] = false;
    }
    int spdSlot = 0;
    for (int kt = std::max(0, spdLo); kt <= spdHi && spdSlot < kNums; kt += 10)
    {
        const float y = speedY(static_cast<float>(kt));
        if (y < tapeBot + numPx || y > tapeTop - numPx || std::fabs(y - cy) < boxH * 0.55f)
        {
            continue;
        }
        const std::string label = whole(static_cast<float>(kt));
        const float x = spdTapeR - 26.0f * s - textWidth(label, numPx) * 0.5f;
        paintGlyph(mSpdNum[spdSlot], "tp-sn" + std::to_string(spdSlot), label, numPx, kInk, x, y);
        mSpdUse[spdSlot] = true;
        ++spdSlot;
    }
    int altSlot = 0;
    for (int ft = altLo; ft <= altHi && altSlot < kNums; ft += 200)
    {
        if (ft < 0)
        {
            continue;
        }
        const float y = altY(static_cast<float>(ft));
        if (y < tapeBot + altNumPx || y > tapeTop - altNumPx || std::fabs(y - cy) < boxH * 0.55f)
        {
            continue;
        }
        const std::string label = whole(static_cast<float>(ft));
        const float x = altTapeL + 22.0f * s + textWidth(label, altNumPx) * 0.5f;
        paintGlyph(mAltNum[altSlot], "tp-an" + std::to_string(altSlot), label, altNumPx, kInk, x, y);
        mAltUse[altSlot] = true;
        ++altSlot;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    mShader.render();

    Glyph *fixed[] = {&mSpdTop, &mAltTop, &mIas, &mAlt, &mVs, &mTasLbl, &mTasVal, &mGsLbl, &mGsVal, &mBaroLbl, &mBaroVal};
    for (Glyph *glyph : fixed)
    {
        if (glyph->draw)
        {
            glyph->draw->render();
        }
    }
    for (int i = 0; i < kNums; ++i)
    {
        if (mSpdUse[i] && mSpdNum[i].draw)
        {
            mSpdNum[i].draw->render();
        }
        if (mAltUse[i] && mAltNum[i].draw)
        {
            mAltNum[i].draw->render();
        }
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
