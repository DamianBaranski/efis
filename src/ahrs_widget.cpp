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
                                                                    mOldAttitudeData{},
                                                                    mNewAttitudeData{}
{
    mDataManager.attach(this, DataType::ATTITUDE_DATA);
    screen.registerRenderer(this);
    mLandRepresentation.drawTexture(std::string(cResourcesPath) + std::string(cLandRepresentationTexture), screen.getWidth() / 2, screen.getHeight() / 2);
    mHorizonLine.drawTexture(std::string(cResourcesPath) + std::string(cHorizonLineTexture), screen.getWidth() / 2, screen.getHeight() / 2);
    mPithScale.drawTexture(std::string(cResourcesPath) + std::string(cPithScaleTexture), screen.getWidth() / 2, screen.getHeight() / 2);
    mRollPointer.drawTexture(std::string(cResourcesPath) + std::string(cRollPointerTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mSkipSkidIndicator.drawTexture(std::string(cResourcesPath) + std::string(cSkipSkidIndicatorTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mAttitudeIndicator.drawTexture(std::string(cResourcesPath) + std::string(cAttitudeIndicatorTexture), screen.getWidth() / 2, cAttitudeYPosition);
    mAircraftSymbol.drawTexture(std::string(cResourcesPath) + std::string(cAircraftSymbolTexture), screen.getWidth() / 2, screen.getHeight() / 2);
}

void AhrsWidget::render() const
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

    mNewAttitudeData = mDataManager.getAttitudeData();
}

void AhrsWidget::updateRenderers() const
{
    if (mOldAttitudeData.pitch == mNewAttitudeData.pitch && mOldAttitudeData.roll == mNewAttitudeData.roll)
    {
        return;
    }

    float newPith = mNewAttitudeData.pitch - mOldAttitudeData.pitch;
    float newRoll = mNewAttitudeData.roll - mOldAttitudeData.roll;
    float rotationCenterX = mScreen.getWidth() / 2 + sin(newRoll) * mOldAttitudeData.pitch;
    float rotationCenterY = mScreen.getHeight() / 2 + cos(newRoll) * mOldAttitudeData.pitch;

    mHorizonLine.setRotation(newRoll, rotationCenterX, rotationCenterY);
    mLandRepresentation.setRotation(newRoll, rotationCenterX, rotationCenterY);
    mPithScale.setRotation(newRoll, rotationCenterX, rotationCenterY);

    mHorizonLine.setPosition(0, newPith);
    mLandRepresentation.setPosition(0, newPith);
    mPithScale.setPosition(0, newPith);

    mAttitudeIndicator.setRotation(newRoll, mScreen.getWidth() / 2, cAttitudeYPosition);
    mOldAttitudeData = mNewAttitudeData;
}