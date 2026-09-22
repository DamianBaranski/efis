/// \file iwidget.h
/// Base type for every drawable that registers itself with the screen.
#ifndef IWIDGET_H
#define IWIDGET_H

#include "screen.h"

/// Drawable that registers with the screen on construction.
/// A disabled widget stays registered and skips its draw.
class IWidget : public IRenderer
{
public:
    /// Registers this widget with the screen. Enabled by default.
    /// \param screen Frame that draws this widget. Must outlive it.
    IWidget(Screen &screen) : mScreen(screen), mEnabled(true) { mScreen.registerRenderer(this); }

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
    Screen &mScreen; ///< Frame this widget registered with.
    bool mEnabled;   ///< False skips the draw. The widget stays registered.
};

#endif // IWIDGET_H
