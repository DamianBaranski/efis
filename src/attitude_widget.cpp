#include "attitude_widget.h"

AttitudeWidget::AttitudeWidget(Screen &screen, IDataManager &dataManager)
    : IWidget(screen), mDataManager(dataManager)
{
    mScreen.registerRenderer(this);
    dataManager.attach(this, DataType::ATTITUDE_DATA);
}

void AttitudeWidget::render() const
{
    glBegin(GL_QUADS);
    glColor3ub(0x52, 0x32, 0x09);
    glVertex2f(-1.0, y1); // x, y
    glVertex2f(1.0, y2);
    glVertex2f(1.0, -1.0);
    glVertex2f(-1.0, -1.0);
    glEnd();
    glBegin(GL_QUADS);
    glColor3ub(0x00, 0x79, 0xFB);
    glVertex2f(-1.0, y1); // x, y
    glVertex2f(1.0, y2);
    glVertex2f(1.0, 1.0);
    glVertex2f(-1.0, 1.0);
    glEnd();
    glBegin(GL_QUADS);
    glColor3ub(0xFF, 0xFF, 0xFF);
    glVertex2f(-1.0, y1); // x, y
    glVertex2f(1.0, y2);
    glVertex2f(1.0, y2 + 0.01);
    glVertex2f(-1.0, y1 + 0.01);
    glEnd();
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
    if(type!=DataType::ATTITUDE_DATA) {
        return;
    }
    
    AttitudeData attitudeData = mDataManager.getAttitudeData();
    y1 = (attitudeData.pitch - attitudeData.roll) / 100;
    y2 = (attitudeData.pitch + attitudeData.roll) / 100;
    mScreen.update();
}
