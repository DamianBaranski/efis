#include "attitude_widget.h"

AttitudeWidget::AttitudeWidget(Screen &screen, IDataManager &dataManager)
    : IWidget(screen), mDataManager(dataManager), mHorizon(screen), mScale(screen)
{
    int overlap = screen.getHeight();
    mScreen.registerRenderer(this);
    dataManager.attach(this, DataType::ATTITUDE_DATA);
    mHorizon.drawRectangle(-overlap / 2, -overlap, mScreen.getWidth() + overlap, mScreen.getHeight() / 2 + overlap, 0x523209FF);
    mHorizon.drawRectangle(-overlap, mScreen.getHeight() / 2, mScreen.getWidth() + overlap, mScreen.getHeight() / 2 + overlap, 0x0000FFFF);

    mScale.drawTexture("../resources/attitude_scale.png", (1024 - 300) / 2, 0, 300, 600);
    mHorizon.setPosition(100, 0);
    mScale.setRotation(3.14 / 4, mScreen.getWidth() / 2, mScreen.getHeight() / 2);
    mHorizon.setRotation(3.14 / 4, mScreen.getWidth() / 2, mScreen.getHeight() / 2);
}

void AttitudeWidget::render() const
{
    if (mEnabled)
    {
        mHorizon.render();
    }
    mScale.render();
}

void AttitudeWidget::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void AttitudeWidget::update(DataType type)
{
    if (type != DataType::ATTITUDE_DATA)
    {
        return;
    }

    AttitudeData attitudeData = mDataManager.getAttitudeData();
    // ToDo add procesing of data
    (void) attitudeData;
}

bool AttitudeWidget::mouseClick(int x, int y)
{
    (void)x;
    (void)y;
    mEnabled = !mEnabled;
    return true;
}