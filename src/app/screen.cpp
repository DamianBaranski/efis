/// \file screen.cpp
/// Opens the GLES window and the GL context.
#include "screen.h"
#include "asset_path.h"
#include "GLES3/gl3.h"
#include <algorithm>
#include <iostream>

namespace
{
void desktopSize(int &width, int &height)
{
    width = 1024;
    height = 600;
}

void androidSize(int &width, int &height)
{
    width = 1;
    height = 1;
    SDL_DisplayMode mode{};
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0)
    {
        width = std::max(mode.w, mode.h);
        height = std::min(mode.w, mode.h);
    }
}
}

Screen::Screen()
{
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

    int width = 0;
    int height = 0;
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    androidSize(width, height);
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_RESIZABLE;
#else
    desktopSize(width, height);
    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
#endif
    mWidth = width;
    mHeight = height;

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

int Screen::getWidth() const
{
    return mWidth;
}

int Screen::getHeight() const
{
    return mHeight;
}

void Screen::beginFrame()
{
    syncSize();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Screen::present()
{
    SDL_GL_SwapWindow(mWindow);
}
