#include "attitude_widget.h"

AttitudeWidget::AttitudeWidget(Screen &screen, IDataManager &dataManager)
    : IWidget(screen), mDataManager(dataManager)
{
    mScreen.registerRenderer(this);
    dataManager.attach(this, DataType::ATTITUDE_DATA);
    mScale.drawTexture("../resources/attitude_scale.png", -0.5,-1.0,1.0,2.0);
}

void AttitudeWidget::render() const
{
    mScale.render();
}

void AttitudeWidget::enable(bool enable)
{
    (void)enable;
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
    //ToDo add procesing of data
}
