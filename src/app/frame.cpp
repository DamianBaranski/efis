/// \file frame.cpp
/// Pumps SDL events and draws the registered renderers until quit.
#include "frame.h"
#include <GLES3/gl3.h>
#include <SDL.h>
#include <SDL_image.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

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

void Frame::setTextInputActive(bool active)
{
    if (active == mTextInputActive)
    {
        return;
    }
    mTextInputActive = active;
    if (active)
    {
        SDL_StartTextInput();
    }
    else
    {
        SDL_StopTextInput();
    }
}

void Frame::setScreenshot(const std::string &path, std::uint32_t delayMs, bool quit)
{
    mScreenshotPath = path;
    mScreenshotDelayMs = delayMs;
    mScreenshotQuit = quit;
    mScreenshotDone = false;
    mScreenshotAt = SDL_GetTicks64();
}

void Frame::captureScreenshot()
{
    const int w = mScreen.physicalWidth();
    const int h = mScreen.physicalHeight();
    if (w <= 0 || h <= 0 || mScreenshotPath.empty())
    {
        return;
    }

    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    GLuint resolveFbo = 0;
    GLuint resolveTex = 0;
    if (samples > 0)
    {
        glGenTextures(1, &resolveTex);
        glBindTexture(GL_TEXTURE_2D, resolveTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &resolveFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, resolveFbo);
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (resolveFbo != 0)
    {
        glDeleteFramebuffers(1, &resolveFbo);
    }
    if (resolveTex != 0)
    {
        glDeleteTextures(1, &resolveTex);
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (surface == nullptr)
    {
        std::cerr << "screenshot surface failed: " << SDL_GetError() << std::endl;
        return;
    }
    for (int y = 0; y < h; ++y)
    {
        const std::uint8_t *src = pixels.data() + static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) * 4u;
        std::uint8_t *dst = static_cast<std::uint8_t *>(surface->pixels) + y * surface->pitch;
        std::memcpy(dst, src, static_cast<size_t>(w) * 4u);
    }

    const int saved = IMG_SavePNG(surface, mScreenshotPath.c_str());
    SDL_FreeSurface(surface);
    if (saved != 0)
    {
        std::cerr << "IMG_SavePNG failed: " << IMG_GetError() << std::endl;
        return;
    }
    std::cout << "Wrote screenshot " << mScreenshotPath << std::endl;
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

void Frame::mapFinger(float nx, float ny, int &x, int &y) const
{
    const int px = static_cast<int>(nx * static_cast<float>(mScreen.physicalWidth()));
    const int py = static_cast<int>(ny * static_cast<float>(mScreen.physicalHeight()));
    mScreen.mapPointer(px, py, x, y);
}

int Frame::fingerSlot(std::int64_t id) const
{
    for (int i = 0; i < 2; ++i)
    {
        if (mFingers[i].down && mFingers[i].id == id)
        {
            return i;
        }
    }
    return -1;
}

int Frame::freeFingerSlot() const
{
    for (int i = 0; i < 2; ++i)
    {
        if (!mFingers[i].down)
        {
            return i;
        }
    }
    return -1;
}

int Frame::liveFingerCount() const
{
    int n = 0;
    for (int i = 0; i < 2; ++i)
    {
        if (mFingers[i].down)
        {
            ++n;
        }
    }
    return n;
}

void Frame::beginPinch()
{
    if (!mFingers[0].down || !mFingers[1].down)
    {
        return;
    }
    const float dx = static_cast<float>(mFingers[1].x - mFingers[0].x);
    const float dy = static_cast<float>(mFingers[1].y - mFingers[0].y);
    mPinchDist = std::hypot(dx, dy);
    mPinchMidX = (mFingers[0].x + mFingers[1].x) / 2;
    mPinchMidY = (mFingers[0].y + mFingers[1].y) / 2;
    mPinching = mPinchDist >= 24.0f;
}

void Frame::applyPinch()
{
    if (!mPinching || !mFingers[0].down || !mFingers[1].down)
    {
        return;
    }
    const float dx = static_cast<float>(mFingers[1].x - mFingers[0].x);
    const float dy = static_cast<float>(mFingers[1].y - mFingers[0].y);
    const float dist = std::hypot(dx, dy);
    const int midX = (mFingers[0].x + mFingers[1].x) / 2;
    const int midY = (mFingers[0].y + mFingers[1].y) / 2;
    if (mPinchDist >= 24.0f && dist >= 24.0f)
    {
        const float ratio = dist / mPinchDist;
        const float dz = std::log(ratio) / std::log(1.8f);
        if (std::fabs(dz) >= 0.01f)
        {
            for (auto *renderer : mInput)
            {
                if (renderer->pinch(midX, midY, dz))
                {
                    break;
                }
            }
        }
    }
    const int pdx = midX - mPinchMidX;
    const int pdy = midY - mPinchMidY;
    if (pdx != 0 || pdy != 0)
    {
        for (auto *renderer : mInput)
        {
            if (renderer->mouseMove(midX, midY, pdx, pdy))
            {
                break;
            }
        }
    }
    mPinchDist = dist;
    mPinchMidX = midX;
    mPinchMidY = midY;
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

            case SDL_TEXTINPUT:
                for (auto *renderer : mInput)
                {
                    if (renderer->textInput(event.text.text))
                    {
                        break;
                    }
                }
                break;

#ifndef __ANDROID__
            case SDL_MOUSEBUTTONDOWN:
            {
                int x = 0;
                int y = 0;
                mScreen.mapPointer(event.button.x, event.button.y, x, y);
                mPointerDown = true;
                mPointerX = x;
                mPointerY = y;
                for (auto *renderer : mInput)
                {
                    if (renderer->mouseClick(x, y))
                    {
                        break;
                    }
                }
                break;
            }

            case SDL_MOUSEBUTTONUP:
            {
                int x = 0;
                int y = 0;
                mScreen.mapPointer(event.button.x, event.button.y, x, y);
                mPointerDown = false;
                for (auto *renderer : mInput)
                {
                    if (renderer->mouseUp(x, y))
                    {
                        break;
                    }
                }
                break;
            }

            case SDL_MOUSEMOTION:
                if (mPointerDown)
                {
                    int x = 0;
                    int y = 0;
                    mScreen.mapPointer(event.motion.x, event.motion.y, x, y);
                    const int dx = x - mPointerX;
                    const int dy = y - mPointerY;
                    mPointerX = x;
                    mPointerY = y;
                    for (auto *renderer : mInput)
                    {
                        if (renderer->mouseMove(x, y, dx, dy))
                        {
                            break;
                        }
                    }
                }
                break;

            case SDL_MOUSEWHEEL:
            {
                int mx = 0;
                int my = 0;
                SDL_GetMouseState(&mx, &my);
                int x = 0;
                int y = 0;
                mScreen.mapPointer(mx, my, x, y);
                for (auto *renderer : mInput)
                {
                    if (renderer->mouseWheel(x, y, event.wheel.y))
                    {
                        break;
                    }
                }
                break;
            }
#endif
            case SDL_FINGERDOWN:
            {
                const int already = liveFingerCount();
                if (already == 0 && !acceptTouch())
                {
                    break;
                }
                const int slot = freeFingerSlot();
                if (slot < 0)
                {
                    break;
                }
                int x = 0;
                int y = 0;
                mapFinger(event.tfinger.x, event.tfinger.y, x, y);
                mFingers[slot].down = true;
                mFingers[slot].id = event.tfinger.fingerId;
                mFingers[slot].x = x;
                mFingers[slot].y = y;
                if (already == 0)
                {
                    mPointerDown = true;
                    mPointerX = x;
                    mPointerY = y;
                    mPinching = false;
                    for (auto *renderer : mInput)
                    {
                        if (renderer->mouseClick(x, y))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    for (auto *renderer : mInput)
                    {
                        renderer->gesturePinchBegan();
                    }
                    beginPinch();
                }
                break;
            }

            case SDL_FINGERMOTION:
            {
                const int slot = fingerSlot(event.tfinger.fingerId);
                if (slot < 0)
                {
                    break;
                }
                int x = 0;
                int y = 0;
                mapFinger(event.tfinger.x, event.tfinger.y, x, y);
                mFingers[slot].x = x;
                mFingers[slot].y = y;
                if (liveFingerCount() == 2)
                {
                    if (!mPinching)
                    {
                        beginPinch();
                    }
                    if (mPinching)
                    {
                        applyPinch();
                    }
                    break;
                }
                if (!mPointerDown || liveFingerCount() != 1)
                {
                    break;
                }
                const int dx = x - mPointerX;
                const int dy = y - mPointerY;
                mPointerX = x;
                mPointerY = y;
                for (auto *renderer : mInput)
                {
                    if (renderer->mouseMove(x, y, dx, dy))
                    {
                        break;
                    }
                }
                break;
            }

            case SDL_FINGERUP:
            {
                const int slot = fingerSlot(event.tfinger.fingerId);
                if (slot < 0)
                {
                    break;
                }
                int x = 0;
                int y = 0;
                mapFinger(event.tfinger.x, event.tfinger.y, x, y);
                mFingers[slot].down = false;
                const int left = liveFingerCount();
                if (left == 1)
                {
                    mPinching = false;
                    for (int i = 0; i < 2; ++i)
                    {
                        if (mFingers[i].down)
                        {
                            mPointerX = mFingers[i].x;
                            mPointerY = mFingers[i].y;
                            mPointerDown = true;
                        }
                    }
                    for (auto *renderer : mInput)
                    {
                        if (renderer->mouseUp(x, y))
                        {
                            break;
                        }
                    }
                }
                else
                {
                    mPinching = false;
                    mPointerDown = false;
                    for (auto *renderer : mInput)
                    {
                        if (renderer->mouseUp(x, y))
                        {
                            break;
                        }
                    }
                }
                break;
            }
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
        if (!mScreenshotDone && !mScreenshotPath.empty() &&
            (SDL_GetTicks64() - mScreenshotAt) >= mScreenshotDelayMs && frames >= 3)
        {
            captureScreenshot();
            mScreenshotDone = true;
            if (mScreenshotQuit)
            {
                quit = true;
            }
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
