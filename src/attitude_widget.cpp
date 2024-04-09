#include "attitude_widget.h"

AttitudeWidget::AttitudeWidget(Screen &screen, IDataManager &dataManager)
    : IWidget(screen), mDataManager(dataManager), mRenderer(screen.getRenderer()) // Pobranie renderera z obiektu Screen
{
    mScreen.registerRenderer(this);
    dataManager.attach(this, DataType::ATTITUDE_DATA);
    mScaleTexture.load("../resources/attitude_scale.png", mRenderer); // Przekazanie renderera do funkcji load
}

void AttitudeWidget::render() const
{
    SDL_SetRenderDrawColor(mRenderer, 0x52, 0x32, 0x09, 0xFF);
    SDL_Rect backgroundRect = {0, 0, mScreen.getWidth(), mScreen.getHeight()};
    SDL_RenderFillRect(mRenderer, &backgroundRect);

    SDL_Color sky_color = SDL_Color{0x00, 0x79, 0xFB, 0xFF};
    SDL_Vertex vertex[] = {{0, 0, sky_color, SDL_FPoint{0, 0}},
                           {mScreen.getWidth(), 0, sky_color, SDL_FPoint{0,0}},
                           {mScreen.getWidth(), mScreen.getHeight() / 2 + y1, sky_color, SDL_FPoint{0, 0}},
                           {0, mScreen.getHeight() / 2 + y2, sky_color, SDL_FPoint{0, 0}}};

    int indices[] = {0, 1, 2, 0, 2, 3};

    SDL_RenderGeometry(mRenderer, nullptr, vertex, sizeof(vertex) / sizeof(SDL_Vertex),
                       indices, sizeof(indices) / sizeof(int));
    
    SDL_SetRenderDrawColor(mRenderer, 0xFF, 0xFF, 0xFF, 0xFF);
    SDL_RenderDrawLine(mRenderer, 0, static_cast<int>(y1), 600, static_cast<int>(y1 + 1));

    mScaleTexture.draw(mRenderer,
                       (mScreen.getWidth() - mScaleTexture.getWidth()) / 2,
                       (mScreen.getHeight() - mScaleTexture.getHeight()) / 2,
                       mScaleTexture.getWidth(),
                       mScaleTexture.getHeight());
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
    y1 = (attitudeData.pitch - attitudeData.roll);
    y2 = (attitudeData.pitch + attitudeData.roll);
    mScreen.update();
}
