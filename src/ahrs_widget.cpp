#include "ahrs_widget.h"
#include <GLES3/gl3.h>

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
    const int x = screen.getWidth() / 2;
    mLandRepresentation.drawTexture(std::string(cResourcesPath) + std::string(cLandRepresentationTexture), x, mAttitudeY);
    mHorizonLine.drawTexture(std::string(cResourcesPath) + std::string(cHorizonLineTexture), x, mAttitudeY);
    mPithScale.drawTexture(std::string(cResourcesPath) + std::string(cPithScaleTexture), x, mAttitudeY);
    mRollPointer.drawTexture(std::string(cResourcesPath) + std::string(cRollPointerTexture), x, mAttitudeY);
    mSkipSkidIndicator.drawTexture(std::string(cResourcesPath) + std::string(cSkipSkidIndicatorTexture), x, mAttitudeY);
    mAttitudeIndicator.drawTexture(std::string(cResourcesPath) + std::string(cAttitudeIndicatorTexture), x, mAttitudeY);
    mAircraftSymbol.drawTexture(std::string(cResourcesPath) + std::string(cAircraftSymbolTexture), x, mAttitudeY);
}

void AhrsWidget::render()
{
    if (!mEnabled)
    {
        return;
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

    float pitchPixels = -mAttitudeData.pitch * cPixelPerPitchRadians;
    float rotationCenterX = mScreen.getWidth() / 2;
    float rotationCenterY = static_cast<float>(mAttitudeY);
    glm::mat4 trans(1.0);
    trans = glm::translate(trans, glm::vec3(rotationCenterX, rotationCenterY, 0));
    // 2D Y-up: negative roll so the tape banks the same way as the 3D horizon.
    trans = glm::rotate(trans, -mAttitudeData.roll, glm::vec3(0, 0, 1.0));
    trans = glm::translate(trans, glm::vec3(-rotationCenterX, -rotationCenterY, 0));

    mAttitudeIndicator.setTransformationMatrix(trans);

    trans = glm::translate(trans, glm::vec3(0, pitchPixels, 0));
    mHorizonLine.setTransformationMatrix(trans);
    mLandRepresentation.setTransformationMatrix(trans);
    mPithScale.setTransformationMatrix(trans);
    mAttitudeDataUpdated = false;
}