#include "ahrs_widget.h"
#include "asset_path.h"
#include <GLES3/gl3.h>
#include <algorithm>

AhrsWidget::AhrsWidget(Screen &screen, IDataManager &dataManager) : IWidget(screen),
                                                                    mDataManager(dataManager),
                                                                    mLandRepresentation(screen),
                                                                    mHorizonLine(screen),
                                                                    mPithScale(screen),
                                                                    mRollPointer(screen),
                                                                    mSkipSkidIndicator(screen),
                                                                    mAttitudeIndicator(screen),
                                                                    mAircraftSymbol(screen),
                                                                    mAttitudeData{},
                                                                    mAttitudeDataUpdated(false),
                                                                    mAttitudeY(screen.getHeight() / 2)
{
    mDataManager.attach(this, DataType::ATTITUDE_DATA);
    rebuildSprites();
}

float AhrsWidget::hudScale() const
{
    const int h = mScreen.getHeight();
    if (h <= 0)
    {
        return 1.0f;
    }
    return static_cast<float>(h) / cDesignHeight;
}

void AhrsWidget::rebuildSprites()
{
    const int w = mScreen.getWidth();
    const int h = mScreen.getHeight();
    const int cx = w / 2;
    const int cy = h / 2;
    // PC art is authored for a 600px-tall window. Grow the tape if the
    // 2048px-wide sky/ground would still leave the sides uncovered.
    const float designScale = hudScale();
    const float scale = std::max(designScale, w > 0 ? static_cast<float>(w) / 2048.0f : designScale);

    mLayoutX = cx;
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
    mAttitudeDataUpdated = true;
}

void AhrsWidget::render()
{
    if (!mEnabled)
    {
        return;
    }
    if (mScreen.getWidth() != mLayoutW || mScreen.getHeight() != mLayoutH)
    {
        rebuildSprites();
    }
    updateRenderers();

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    if (mDrawSkyGround)
    {
        mLandRepresentation.render();
        mHorizonLine.render();
    }
    mPithScale.render();
    mRollPointer.render();
    mSkipSkidIndicator.render();
    mAttitudeIndicator.render();
    mAircraftSymbol.render();

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
    mAttitudeDataUpdated = true;
}

void AhrsWidget::updateRenderers()
{
    if (!mAttitudeDataUpdated)
    {
        return;
    }

    float pitchPixels = -mAttitudeData.pitch * cPixelPerPitchRadians * mHudScale;
    float rotationCenterX = mScreen.getWidth() / 2;
    float rotationCenterY = static_cast<float>(mAttitudeY);
    glm::mat4 trans(1.0);
    trans = glm::translate(trans, glm::vec3(rotationCenterX, rotationCenterY, 0));
    // 2D Y-up: negative roll so the tape banks the same way as the 3D horizon.
    trans = glm::rotate(trans, -mAttitudeData.roll, glm::vec3(0, 0, 1.0));
    trans = glm::translate(trans, glm::vec3(-rotationCenterX, -rotationCenterY, 0));

    mAttitudeIndicator.setTransformationMatrix(trans);
    mRollPointer.setTransformationMatrix(glm::mat4(1.0));
    mSkipSkidIndicator.setTransformationMatrix(glm::mat4(1.0));
    mAircraftSymbol.setTransformationMatrix(glm::mat4(1.0));

    trans = glm::translate(trans, glm::vec3(0, pitchPixels, 0));
    mHorizonLine.setTransformationMatrix(trans);
    mLandRepresentation.setTransformationMatrix(trans);
    mPithScale.setTransformationMatrix(trans);
    mAttitudeDataUpdated = false;
}