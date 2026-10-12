/// \file ahrs_widget.cpp
/// Draws the shared attitude tapes from the latest attitude sample.
#include "ahrs_widget.h"
#include "asset_path.h"
#include "split_layout.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace
{
template <std::size_t... I>
std::array<Render2D, sizeof...(I)> pitchLabels(const Screen &screen, std::index_sequence<I...>)
{
    return {{((void)I, Render2D(screen))...}};
}

template <std::size_t N>
std::array<Render2D, N> pitchLabels(const Screen &screen)
{
    return pitchLabels(screen, std::make_index_sequence<N>{});
}
} // namespace

AhrsWidget::AhrsWidget(Frame &frame, IDataManager &dataManager) : IWidget(frame),
                                                                    mDataManager(dataManager),
                                                                    mLandRepresentation(frame.screen()),
                                                                    mHorizonLine(frame.screen()),
                                                                    mPithScale(frame.screen()),
                                                                    mRollPointer(frame.screen()),
                                                                    mSkipSkidIndicator(frame.screen()),
                                                                    mAttitudeIndicator(frame.screen()),
                                                                    mAircraftSymbol(frame.screen()),
                                                                    mPitchLabel(pitchLabels<AhrsWidget::cPitchLabelCount>(frame.screen())),
                                                                    mAttitudeData{},
                                                                    mAttitudeY(frame.screen().getHeight() / 2)
{
    mDataManager.attach(this, DataType::ATTITUDE_DATA);
    rebuildSprites();
}

void AhrsWidget::setSplit(bool split)
{
    if (mSplit == split)
    {
        return;
    }
    mSplit = split;
    mLayoutW = -1;
}

namespace
{
constexpr float kPi = 3.14159265f;
constexpr uint32_t kRuler = 0xFFFFFFFFu;

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
    addQuad(dst, x, y, x + w, y, x + w, y + h, x, y + h);
}

void addSegment(Triangles &dst, float x0, float y0, float x1, float y1, float thickness)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::hypot(dx, dy);
    if (len < 0.001f)
    {
        return;
    }
    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;
    addQuad(dst, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny);
}

Triangles rulerMesh()
{
    Triangles mesh;
    mesh.material = "ahrs-ruler";
    return mesh;
}
} // namespace

float AhrsWidget::hudScale() const
{
    const int h = mLayoutH > 0 ? mLayoutH : mScreen.getHeight();
    if (h <= 0)
    {
        return 1.0f;
    }
    return static_cast<float>(h) / cDesignHeight * 1.12f;
}

void AhrsWidget::buildRulers(float cx, float cy, float scale)
{
    const float thick = std::max(2.0f, 2.0f * scale);
    Triangles horizon = rulerMesh();
    // The old horizon art was a 2048px line. A quad stays sharp at any scale.
    addRect(horizon, cx - 1024.0f * scale, cy - thick * 0.5f, 2048.0f * scale, thick);
    mHorizonLine.setSolidMesh({horizon}, kRuler);

    // Open sky on the yellow symbol, left and right: between the wing bar and the
    // arrow's widest point. That gap is centered 92 design px off the symbol.
    // Numbers land there. Long bars stop before the numbers; the middle stays solid.
    const float band = cy;
    constexpr float kLabelX = 92.0f;
    int slot = 0;
    const float xOff = kLabelX * scale;
    // Same size as the altitude-tape figures (tape_widget altNumPx at this scale).
    const float tapeNum = std::max(10.0f, 15.0f * scale);
    const float altTapeW = 64.0f * scale;
    const float font = std::max(9.0f, std::min(tapeNum, (altTapeW - 24.0f * scale) / 3.0f));
    for (int deg = 10; deg <= 90; deg += 10)
    {
        for (int sign : {1, -1})
        {
            const int value = sign * deg;
            const float y = band + static_cast<float>(value) * cPitchPixelsPerDegree * scale;
            const std::string text = std::to_string(value);
            mPitchLabel[static_cast<size_t>(slot)].drawTextCentered(text, font, cx - xOff, y, 0xFFFFFFFFu,
                                                                    "ahrs-pitch-l" + text);
            mPitchLabel[static_cast<size_t>(slot + 1)].drawTextCentered(text, font, cx + xOff, y, 0xFFFFFFFFu,
                                                                        "ahrs-pitch-r" + text);
            slot += 2;
        }
    }
    buildVisibleLadder(mPitchDeg);

    // Bank arc. The old 340px plate put the circle 5px below the wings.
    // Lifted by 10% of the arc radius so more pitch fits under it.
    constexpr float kRollLift = 15.5f;
    const float arcX = cx;
    const float arcY = cy + (kRollLift - 5.0f) * scale;
    const float radius = 155.0f * scale;
    const float arcThick = thick;
    Triangles bank = rulerMesh();
    constexpr int kArcSteps = 48;
    const float a0 = -60.0f * kPi / 180.0f;
    const float a1 = 60.0f * kPi / 180.0f;
    for (int i = 0; i < kArcSteps; ++i)
    {
        const float t0 = a0 + (a1 - a0) * static_cast<float>(i) / kArcSteps;
        const float t1 = a0 + (a1 - a0) * static_cast<float>(i + 1) / kArcSteps;
        auto at = [&](float t, float r) {
            return std::pair<float, float>(arcX + std::sin(t) * r, arcY + std::cos(t) * r);
        };
        const auto o0 = at(t0, radius + arcThick * 0.5f);
        const auto i0 = at(t0, radius - arcThick * 0.5f);
        const auto o1 = at(t1, radius + arcThick * 0.5f);
        const auto i1 = at(t1, radius - arcThick * 0.5f);
        addQuad(bank, o0.first, o0.second, o1.first, o1.second, i1.first, i1.second, i0.first, i0.second);
    }
    auto tick = [&](float deg, float outer) {
        const float t = deg * kPi / 180.0f;
        addSegment(bank, arcX + std::sin(t) * radius, arcY + std::cos(t) * radius, arcX + std::sin(t) * outer * scale,
                   arcY + std::cos(t) * outer * scale, thick);
    };
    for (float deg : {10.0f, 20.0f, 30.0f, 45.0f, 60.0f, -10.0f, -20.0f, -30.0f, -45.0f, -60.0f})
    {
        const float mag = std::fabs(deg);
        float outer = 165.0f;
        if (mag == 30.0f || mag == 60.0f)
        {
            outer = 171.0f;
        }
        else if (mag == 45.0f)
        {
            outer = 163.0f;
        }
        tick(deg, outer);
    }
    // Equilateral sky pointer just outside the arc. Point faces the arc.
    constexpr float kSide = 22.0f;
    const float kHeight = kSide * 0.8660254f;
    const float tipR = 155.0f + thick / scale * 0.5f + 2.0f;
    const float baseR = tipR + kHeight;
    addTri(bank, arcX, arcY + tipR * scale, arcX - (kSide * 0.5f) * scale, arcY + baseR * scale,
           arcX + (kSide * 0.5f) * scale, arcY + baseR * scale);
    mAttitudeIndicator.setSolidMesh({bank}, kRuler);

    Triangles index = rulerMesh();
    addTri(index, cx, cy + (148.0f + kRollLift) * scale, cx - 9.0f * scale, cy + (134.0f + kRollLift) * scale,
           cx + 9.0f * scale, cy + (134.0f + kRollLift) * scale);
    mRollPointer.setSolidMesh({index}, kRuler);
}

void AhrsWidget::buildVisibleLadder(float pitchDeg)
{
    const float cx = static_cast<float>(mLayoutX);
    const float cy = static_cast<float>(mAttitudeY);
    const float scale = mHudScale > 0.0f ? mHudScale : 1.0f;
    const float thick = std::max(2.0f, 2.0f * scale);
    constexpr float kLabelX = 92.0f;
    constexpr float kLabelClear = 18.0f;
    const float longHalf = kLabelX - kLabelClear;
    const float shortHalf = longHalf * 0.5f;
    Triangles ladder = rulerMesh();
    for (int step = -36; step <= 36; ++step)
    {
        const int mag = std::abs(step);
        if (mag % 2 != 0)
        {
            continue;
        }
        const float deg = static_cast<float>(step) * 2.5f;
        // Whole bars only. A bar on the 30° boundary stays; the next one stays out.
        if (std::fabs(deg - pitchDeg) > 30.01f)
        {
            continue;
        }
        const bool longBar = mag % 4 == 0;
        const bool numbered = longBar && mag != 0;
        const float half = longBar ? longHalf : shortHalf;
        const float y = cy + deg * cPitchPixelsPerDegree * scale;
        const float y0 = y - thick * 0.5f;
        if (numbered)
        {
            const float halfPx = half * scale;
            const float label = kLabelX * scale;
            const float clear = kLabelClear * scale;
            const float outerW = halfPx - (label + clear);
            if (outerW > 1.0f)
            {
                addRect(ladder, cx - halfPx, y0, outerW, thick);
                addRect(ladder, cx + label + clear, y0, outerW, thick);
            }
            addRect(ladder, cx - (label - clear), y0, (label - clear) * 2.0f, thick);
        }
        else
        {
            addRect(ladder, cx - half * scale, y0, half * 2.0f * scale, thick);
        }
    }
    mPithScale.setSolidMesh({ladder}, kRuler);
}

void AhrsWidget::rebuildSprites()
{
    const int screenW = std::max(1, mScreen.getWidth());
    const int screenH = std::max(1, mScreen.getHeight());
    const SplitLayout layout = mSplit ? makeSplitLayout(screenW, screenH) : SplitLayout{};
    const int w = mSplit ? std::max(1, layout.ahrs.w) : screenW;
    const int h = mSplit ? std::max(1, layout.ahrs.h) : screenH;
    const int cx = mSplit ? layout.ahrs.x + w / 2 : w / 2;
    const int cy = mSplit ? screenH - layout.ahrs.y - h / 2 : h / 2;
    if (mSplit)
    {
        mClipX = layout.ahrsClip.x;
        mClipY = layout.ahrsClip.y;
        mClipW = layout.ahrsClip.w;
        mClipH = layout.ahrsClip.h;
    }
    else
    {
        mClipX = 0;
        mClipY = 0;
        mClipW = 0;
        mClipH = 0;
    }
    // PC art is authored for a 600px-tall window. Grow the tape if the
    // 2048px-wide sky/ground would still leave the sides uncovered.
    // 12% above that design. The route strip keeps its own scale.
    const float designScale = static_cast<float>(h) / cDesignHeight;
    const float scale = std::max(designScale, w > 0 ? static_cast<float>(w) / 2048.0f : designScale) * 1.12f;

    mLayoutX = cx;
    mPaneX = mSplit ? layout.ahrs.x : 0;
    mAttitudeY = cy;
    mLayoutW = w;
    mLayoutH = h;
    mHudScale = scale;

    const std::string ahrsDir = AssetPath::resolve("resources/textures/ui/AHRS") + "/";
    mLandRepresentation.drawTexture(ahrsDir + std::string(cLandRepresentationTexture), cx, cy, scale);
    mSkipSkidIndicator.drawTexture(ahrsDir + std::string(cSkipSkidIndicatorTexture), cx, cy, scale);
    mAircraftSymbol.drawTexture(ahrsDir + std::string(cAircraftSymbolTexture), cx, cy, scale);
    buildRulers(static_cast<float>(cx), static_cast<float>(cy), scale);
}

void AhrsWidget::render()
{
    if (!mEnabled)
    {
        return;
    }
    const int screenW = std::max(1, mScreen.getWidth());
    const int screenH = std::max(1, mScreen.getHeight());
    const SplitLayout layout = mSplit ? makeSplitLayout(screenW, screenH) : SplitLayout{};
    const int wantW = mSplit ? layout.ahrs.w : screenW;
    const int wantH = mSplit ? layout.ahrs.h : screenH;
    const int wantX = mSplit ? layout.ahrs.x : 0;
    if (wantW != mLayoutW || wantH != mLayoutH || wantX != mPaneX)
    {
        rebuildSprites();
    }
    updateRenderers();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    if (mSplit && mClipW > 0 && mClipH > 0)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(mClipX, screenH - (mClipY + mClipH), mClipW, mClipH);
    }

    glDisable(GL_CULL_FACE);
    if (mDrawSkyGround)
    {
        mLandRepresentation.render();
        mHorizonLine.render();
    }
    mPithScale.render();
    int slot = 0;
    for (int deg = 10; deg <= 90; deg += 10)
    {
        for (int sign : {1, -1})
        {
            const float value = static_cast<float>(sign * deg);
            if (std::fabs(value - mPitchDeg) <= 30.01f)
            {
                mPitchLabel[static_cast<size_t>(slot)].render();
                mPitchLabel[static_cast<size_t>(slot + 1)].render();
            }
            slot += 2;
        }
    }
    mAttitudeIndicator.render();
    mSkipSkidIndicator.render();
    mRollPointer.render();
    mAircraftSymbol.render();

    if (mSplit)
    {
        glDisable(GL_SCISSOR_TEST);
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void AhrsWidget::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void AhrsWidget::update(DataType type)
{
    if (type != DataType::ATTITUDE_DATA)
    {
        return;
    }

    mAttitudeData = mDataManager.getAttitudeData();
}

namespace
{
glm::vec3 rotateBody(const AttitudeData &attitude, float vx, float vy, float vz)
{
    const float qw = attitude.qw;
    const float qx = attitude.qx;
    const float qy = attitude.qy;
    const float qz = attitude.qz;
    const float tx = 2.0f * (qy * vz - qz * vy);
    const float ty = 2.0f * (qz * vx - qx * vz);
    const float tz = 2.0f * (qx * vy - qy * vx);
    return glm::vec3(vx + qw * tx + (qy * tz - qz * ty), vy + qw * ty + (qz * tx - qx * tz),
                     vz + qw * tz + (qx * ty - qy * tx));
}

/// Pitch and roll for the tape. Positive pitch is nose up on the screen.
/// Euler roll jumps to 180° when pitch passes vertical, which turns the ladder over
/// during a wings-level tilt. The quaternion keeps that roll at zero.
void tapeAngles(const AttitudeData &attitude, float &pitch, float &roll)
{
    if (attitude.useQuat)
    {
        const glm::vec3 forward = rotateBody(attitude, 1.0f, 0.0f, 0.0f);
        const glm::vec3 up = rotateBody(attitude, 0.0f, 0.0f, -1.0f);
        const glm::vec3 right = rotateBody(attitude, 0.0f, 1.0f, 0.0f);
        // Nose elevation. The adjacent side must be the horizontal part of the
        // nose, not -up.z. -up.z shrinks with cos(roll), so a level bank turned
        // a few degrees of pitch into a ladder that left the center.
        const float horiz = std::hypot(forward.x, forward.y);
        pitch = std::atan2(-forward.z, horiz);
        roll = -std::atan2(right.z, -up.z);
        return;
    }
    pitch = -attitude.pitch;
    roll = attitude.roll;
}
} // namespace

void AhrsWidget::updateRenderers()
{
    // The 3D view reads the feed every frame. The tape must do the same,
    // or a missed publish leaves the ladder sitting still while the horizon banks.
    mAttitudeData = mDataManager.getAttitudeData();
    float pitch = 0.0f;
    float roll = 0.0f;
    tapeAngles(mAttitudeData, pitch, roll);
    mPitchDeg = pitch * (180.0f / kPi);
    buildVisibleLadder(mPitchDeg);
    float pitchPixels = -pitch * cPixelPerPitchRadians * mHudScale;
    float rotationCenterX = static_cast<float>(mLayoutX);
    float rotationCenterY = static_cast<float>(mAttitudeY);
    glm::mat4 trans(1.0);
    trans = glm::translate(trans, glm::vec3(rotationCenterX, rotationCenterY, 0));
    // 2D Y-up: negative roll so the tape banks the same way as the 3D horizon.
    trans = glm::rotate(trans, -roll, glm::vec3(0, 0, 1.0));
    trans = glm::translate(trans, glm::vec3(-rotationCenterX, -rotationCenterY, 0));

    // Ground pointer: the bank arc rolls with the horizon. The index stays with the wings.
    mAttitudeIndicator.setTransformationMatrix(trans);
    mRollPointer.setTransformationMatrix(glm::mat4(1.0f));
    mSkipSkidIndicator.setTransformationMatrix(glm::mat4(1.0f));
    mAircraftSymbol.setTransformationMatrix(glm::mat4(1.0f));

    // The ladder slides along the horizon's vertical, then banks with it.
    trans = glm::translate(trans, glm::vec3(0, pitchPixels, 0));
    mHorizonLine.setTransformationMatrix(trans);
    mLandRepresentation.setTransformationMatrix(trans);
    mPithScale.setTransformationMatrix(trans);
    for (Render2D &label : mPitchLabel)
    {
        label.setTransformationMatrix(trans);
    }
}