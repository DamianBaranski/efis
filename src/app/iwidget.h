/// \file iwidget.h
/// Base type for every drawable that registers itself with the frame.
#ifndef IWIDGET_H
#define IWIDGET_H

#include "frame.h"

/// Drawable measured against a window.
/// The owner adds it to the frame. A disabled widget stays registered and skips its draw.
class IWidget : public IRenderer
{
public:
    /// Enabled by default. Call Frame::add to draw it.
    /// \param frame Window this widget measures against. Must outlive it.
    IWidget(Frame &frame) : mScreen(frame.screen()), mEnabled(true) {}

    /// Draws the widget. Required.
    virtual void render() = 0;

    /// Shows or hides the widget without removing it from the frame.
    /// \param enable True draws on the next frame.
    virtual void enable(bool enable) { mEnabled = enable; }

    /// Places the widget. Pixels, origin at the top left.
    /// \param x Left edge.
    /// \param y Top edge.
    virtual void setPos(int x, int y) = 0;

protected:
    Screen &mScreen; ///< Window this widget measures against.
    bool mEnabled;   ///< False skips the draw. The widget stays registered.
};

#endif // IWIDGET_H
