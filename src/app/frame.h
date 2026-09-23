/// \file frame.h
/// Event dispatch and the ordered list of things drawn each frame.

#ifndef FRAME_H
#define FRAME_H

#include "screen.h"
#include <cstdint>
#include <functional>
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

    /// Pumps events and draws until the window closes.
    void run();

private:
    bool acceptTouch();

    Screen &mScreen;
    std::vector<IRenderer *> mDraw;
    std::vector<IRenderer *> mInput;
    std::function<void()> mTick;
    uint64_t mTouchReadyAt = 0;
    uint64_t mLastTouchMs = 0;
};

#endif
