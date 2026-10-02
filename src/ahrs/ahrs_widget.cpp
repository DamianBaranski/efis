/// \file ahrs_widget.cpp
/// Draws the shared attitude tapes from the latest attitude sample.
#include "ahrs_widget.h"
#include "asset_path.h"
#include "split_layout.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>

AhrsWidget::AhrsWidget(Frame &frame, IDataManager &dataManager) : IWidget(frame),
                                                                    mDataManager(dataManager),
                                                                    mLandRepresentation(frame.screen()),
                                                                    mHorizonLine(frame.screen()),
                                                                    mPithScale(frame.screen()),
                                                                    mRollPointer(frame.screen()),
                                                                    mSkipSkidIndicator(frame.screen()),
                                                                    mAttitudeIndicator(frame.screen()),
                                                                    mAircraftSymbol(frame.screen()),
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

float AhrsWidget::hudScale() const
{
    const int h = mLayoutH > 0 ? mLayoutH : mScreen.getHeight();
    if (h <= 0)
    {
        return 1.0f;
    }
    return static_cast<float>(h) / cDesignHeight;
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
    const float designScale = static_cast<float>(h) / cDesignHeight;
    const float scale = std::max(designScale, w > 0 ? static_cast<float>(w) / 2048.0f : designScale);

    mLayoutX = cx;
    mPaneX = mSplit ? layout.ahrs.x : 0;
    mAttitudeY = cy;
    mLayoutW = w;
    mLayoutH = h;
    mHudScale = scale;

    const std::string ahrsDir = AssetPath::resolve("resources/textures/ui/AHRS") + "/";
    mLandRepresentation.drawTexture(ahrsDir + std::string(cLandRepresentationTexture), cx, cy, scale);
    mHorizonLine.drawTexture(ahrsDir + std::string(cHorizonLineTexture), cx, cy, scale);
    mPithScale.drawTexture(ahrsDir + std::string(cPithScaleTexture), cx, cy, scale);
    mRollPointer.drawTexture(ahrsDir + std::string(cRollPointerTexture), cx, cy, scale);
    mSkipSkidIndicator.drawTexture(ahrsDir + std::string(cSkipSkidIndicatorTexture), cx, cy, scale);
    mAttitudeIndicator.drawTexture(ahrsDir + std::string(cAttitudeIndicatorTexture), cx, cy, scale);
    mAircraftSymbol.drawTexture(ahrsDir + std::string(cAircraftSymbolTexture), cx, cy, scale);
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

    if (mDrawSkyGround)
    {
        mLandRepresentation.render();
        mHorizonLine.render();
    }
    mPithScale.render();
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
}