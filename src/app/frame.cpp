/// \file frame.cpp
/// Pumps SDL events and draws the registered renderers until quit.
#include "frame.h"
#include <iostream>

Frame::Frame(Screen &screen) : mScreen(screen) {}

void Frame::add(IRenderer *renderer)
{
    mDraw.push_back(renderer);
    mInput.push_back(renderer);
}

void Frame::addInputFront(IRenderer *renderer)
{
    mInput.insert(mInput.begin(), renderer);
}

void Frame::setTick(std::function<void()> tick)
{
    mTick = std::move(tick);
}

bool Frame::acceptTouch()
{
    const uint64_t now = SDL_GetTicks64();
    if (now < mTouchReadyAt || now - mLastTouchMs < 120)
    {
        return false;
    }
    mLastTouchMs = now;
    return true;
}

void Frame::run()
{
    int frames = 0;
    uint64_t start = SDL_GetTicks64();
    mTouchReadyAt = start + 2000;
    bool quit = false;
    while (!quit)
    {
        ++frames;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                quit = true;
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_RESIZED)
                {
                    mScreen.syncSize();
                }
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    quit = true;
                    break;
                }
                for (auto *renderer : mInput)
                {
                    if (renderer->keyDown(event.key.keysym.sym))
                    {
                        break;
                    }
                }
                break;

#ifndef __ANDROID__
            case SDL_MOUSEBUTTONDOWN:
                for (auto *renderer : mInput)
                {
                    if (renderer->mouseClick(event.button.x, event.button.y))
                    {
                        break;
                    }
                }
                break;
#else
            case SDL_FINGERDOWN:
            {
                if (!acceptTouch())
                {
                    break;
                }
                const int x = static_cast<int>(event.tfinger.x * static_cast<float>(mScreen.getWidth()));
                const int y = static_cast<int>(event.tfinger.y * static_cast<float>(mScreen.getHeight()));
                for (auto *renderer : mInput)
                {
                    if (renderer->mouseClick(x, y))
                    {
                        break;
                    }
                }
                break;
            }
#endif
            }
        }
        if (mTick)
        {
            mTick();
        }
        mScreen.beginFrame();
        for (auto *renderer : mDraw)
        {
            renderer->render();
        }
        mScreen.present();
        if ((SDL_GetTicks64() - start) >= 1000)
        {
            std::cout << frames << "FPS" << std::endl;
            frames = 0;
            start = SDL_GetTicks64();
        }
    }
}
