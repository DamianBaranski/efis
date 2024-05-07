#include "ahrs_widget.h"

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
                                                                    mAttitudeDataUpdated(false)
{
    mDataManager.attach(this, DataType::ATTITUDE_DATA);
    screen.registerRenderer(this);
    mLandRepresentation.drawTexture(std::string(cResourcesPath) + std::string(cLandRepresentationTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mHorizonLine.drawTexture(std::string(cResourcesPath) + std::string(cHorizonLineTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mPithScale.drawTexture(std::string(cResourcesPath) + std::string(cPithScaleTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mRollPointer.drawTexture(std::string(cResourcesPath) + std::string(cRollPointerTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mSkipSkidIndicator.drawTexture(std::string(cResourcesPath) + std::string(cSkipSkidIndicatorTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mAttitudeIndicator.drawTexture(std::string(cResourcesPath) + std::string(cAttitudeIndicatorTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mAircraftSymbol.drawTexture(std::string(cResourcesPath) + std::string(cAircraftSymbolTexture), screen.getWidth() / 2, cAttitudeYPosition);
}

void AhrsWidget::render()
{
    updateRenderers();
    mLandRepresentation.render();
    mHorizonLine.render();
    mPithScale.render();
    mRollPointer.render();
    mSkipSkidIndicator.render();
    mAttitudeIndicator.render();
    mAircraftSymbol.render();
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

    float pitchPixels = mAttitudeData.pitch * cPixelPerPitchRadians;
    float rotationCenterX = mScreen.getWidth() / 2;
    float rotationCenterY = cAttitudeYPosition;
    glm::mat4 trans(1.0);
    trans = glm::translate(trans, glm::vec3(rotationCenterX, rotationCenterY, 0));
    trans = glm::rotate(trans, mAttitudeData.roll, glm::vec3(0, 0, 1.0));
    trans = glm::translate(trans, glm::vec3(-rotationCenterX, -rotationCenterY, 0));

    mAttitudeIndicator.setTransformationMatrix(trans);

    trans = glm::translate(trans, glm::vec3(0, pitchPixels, 0));
    mHorizonLine.setTransformationMatrix(trans);
    mLandRepresentation.setTransformationMatrix(trans);
    mPithScale.setTransformationMatrix(trans);
    mAttitudeDataUpdated = false;
}