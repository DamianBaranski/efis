/// \file frame.h
/// Event dispatch and the ordered list of things drawn each frame.

#ifndef FRAME_H
#define FRAME_H

#include "screen.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/// Pumps SDL events, runs a per-frame tick, then draws the registered renderers.
class Frame
{
public:
    /// Binds the loop to an open window. The window must outlive the frame.
    explicit Frame(Screen &screen);

    /// Window this loop presents to.
    Screen &screen() { return mScreen; }

    /// Draws this renderer after the ones already added. It also receives input
    /// after the ones already added, unless addInputFront placed something ahead of it.
    /// \param renderer Not owned. Must outlive the frame.
    void add(IRenderer *renderer);

    /// Offers this renderer events before the renderers added with add.
    /// It is not drawn. Use this for key handling that should run first.
    /// \param renderer Not owned. Must outlive the frame.
    void addInputFront(IRenderer *renderer);

    /// Called after events and before the draw list. Empty by default.
    void setTick(std::function<void()> tick);

    /// Opens or closes SDL text input (soft keyboard on Android).
    /// Widgets that expect typed characters call this from their focus handlers.
    void setTextInputActive(bool active);

    /// True while SDL text input is being routed through the widget stack.
    bool textInputActive() const { return mTextInputActive; }

    /// Writes a PNG of the framebuffer after `delayMs`, then optionally quits.
    /// Used by `--screenshot` to capture the planner Map tab.
    void setScreenshot(const std::string &path, std::uint32_t delayMs = 1500, bool quit = true);

    /// Pumps events and draws until the window closes.
    void run();

private:
    struct TrackedFinger
    {
        bool down = false;
        std::int64_t id = 0;
        int x = 0;
        int y = 0;
    };

    bool acceptTouch();
    void captureScreenshot();
    void mapFinger(float nx, float ny, int &x, int &y) const;
    int fingerSlot(std::int64_t id) const;
    int freeFingerSlot() const;
    int liveFingerCount() const;
    void beginPinch();
    void applyPinch();

    Screen &mScreen;
    std::vector<IRenderer *> mDraw;
    std::vector<IRenderer *> mInput;
    std::function<void()> mTick;
    uint64_t mTouchReadyAt = 0;
    uint64_t mLastTouchMs = 0;
    bool mPointerDown = false;
    int mPointerX = 0;
    int mPointerY = 0;
    TrackedFinger mFingers[2];
    bool mPinching = false;
    float mPinchDist = 0.0f;
    int mPinchMidX = 0;
    int mPinchMidY = 0;
    bool mTextInputActive = false;
    std::string mScreenshotPath;
    std::uint32_t mScreenshotDelayMs = 1500;
    std::uint64_t mScreenshotAt = 0;
    bool mScreenshotQuit = true;
    bool mScreenshotDone = false;
};

#endif
