/// \file screen.cpp
/// Opens the GLES window and pumps SDL events until quit.
#include "screen.h"
#include "asset_path.h"
#include "GLES3/gl3.h"
#include <algorithm>

Screen *Screen::instance = nullptr;

Screen::Screen(int width, int height) : mWidth(width), mHeight(height)
{
    instance = this;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        exit(1);
    }
    AssetPath::init();

#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_DisplayMode mode{};
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
    {
        width = std::max(mode.w, mode.h);
        height = std::min(mode.w, mode.h);
    }
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE;
#else
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
#endif

    mWindow = SDL_CreateWindow("EFIS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!mWindow)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        mWindow = SDL_CreateWindow("EFIS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
    }
    if (!mWindow)
    {
        std::cerr << "SDL window creation failed: " << SDL_GetError() << std::endl;
        exit(1);
    }

    mContext = SDL_GL_CreateContext(mWindow);
    syncSize();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SDL_GL_SwapWindow(mWindow);
}

Screen::~Screen()
{
    if (mWindow)
    {
        SDL_DestroyWindow(mWindow);
    }
    SDL_Quit();
}

void Screen::registerRenderer(IRenderer *renderer)
{
    mRenderers.push_back(renderer);
}

void Screen::registerController(IRenderer *controller)
{
    mRenderers.insert(mRenderers.begin(), controller);
}

void Screen::syncSize()
{
    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(mWindow, &w, &h);
    if (w <= 0 || h <= 0)
    {
        SDL_GetWindowSize(mWindow, &w, &h);
    }
    if (w > 0 && h > 0 && (w != mWidth || h != mHeight))
    {
        mWidth = w;
        mHeight = h;
        std::cout << "Window " << mWidth << "x" << mHeight << std::endl;
    }
    if (mWidth > 0 && mHeight > 0)
    {
        glViewport(0, 0, mWidth, mHeight);
    }
}

bool Screen::acceptTouch()
{
    const uint64_t now = SDL_GetTicks64();
    if (now < mTouchReadyAt || now - mLastTouchMs < 120)
    {
        return false;
    }
    mLastTouchMs = now;
    return true;
}

void Screen::mainLoop()
{
    int i = 0;
    uint64_t start = SDL_GetTicks64();
    mTouchReadyAt = start + 2000;
    bool quit = false;
    while (!quit)
    {
        i++;
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
                    syncSize();
                }
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    quit = true;
                    break;
                }
                for (auto renderer : mRenderers)
                {
                    if (renderer->keyDown(event.key.keysym.sym))
                    {
                        break;
                    }
                }
                break;

#ifndef __ANDROID__
            case SDL_MOUSEBUTTONDOWN:
                for (auto renderer : mRenderers)
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
                const int x = static_cast<int>(event.tfinger.x * static_cast<float>(mWidth));
                const int y = static_cast<int>(event.tfinger.y * static_cast<float>(mHeight));
                for (auto renderer : mRenderers)
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
        render();
        if ((SDL_GetTicks64() - start) >= 1000)
        {
            std::cout << i << "FPS" << std::endl;
            i = 0;
            start = SDL_GetTicks64();
        }
    }
}

int Screen::getWidth() const
{
    return mWidth;
}

int Screen::getHeight() const
{
    return mHeight;
}

void Screen::displayWrapper()
{
    if (instance)
    {
        instance->render();
    }
}

void Screen::render()
{
    syncSize();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (auto renderer : mRenderers)
    {
        renderer->render();
    }

    SDL_GL_SwapWindow(mWindow);
}
