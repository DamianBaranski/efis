/// \file screen.h
/// GLES window. The frame loop lives in Frame.

#ifndef SCREEN_H
#define SCREEN_H

#include "sdl_compat.h"

/// One drawable or input handler in the frame.
/// Draw order and input order are chosen by Frame.
class IRenderer
{
public:
    /// Draws this object for the current frame.
    virtual void render() = 0;

    /// Handles one click or tap.
    /// \param x Pixels from the left. SDL origin, top left.
    /// \param y Pixels from the top.
    /// \return True when this object consumed the event.
    virtual bool mouseClick(int x, int y)
    {
        (void)x;
        (void)y;
        return false;
    }

    /// Handles one key press.
    /// \param key SDL key code.
    /// \return True when this object consumed the key.
    virtual bool keyDown(SDL_Keycode key)
    {
        (void)key;
        return false;
    }

    /// Handles a text input character (typed via keyboard).
    /// \param text UTF-8 text, typically one glyph.
    /// \return True when this object consumed the input.
    virtual bool textInput(const char *text)
    {
        (void)text;
        return false;
    }

    /// Handles pointer motion. Only fired when a button is down (or a finger is
    /// on the surface). `dx`/`dy` are in layout pixels.
    /// \return True when this object consumed the motion.
    virtual bool mouseMove(int x, int y, int dx, int dy)
    {
        (void)x;
        (void)y;
        (void)dx;
        (void)dy;
        return false;
    }

    /// Handles pointer release. Called when the mouse button lifts or the finger
    /// leaves the surface.
    virtual bool mouseUp(int x, int y)
    {
        (void)x;
        (void)y;
        return false;
    }

    /// Handles a mouse wheel tick. `dy` is positive when scrolling up.
    virtual bool mouseWheel(int x, int y, int dy)
    {
        (void)x;
        (void)y;
        (void)dy;
        return false;
    }

    /// Handles a two-finger pinch. `dz` is the change in planner zoom units
    /// (same scale as `zoomFine`). `(x, y)` is the pinch midpoint in layout pixels.
    /// \return True when this object consumed the pinch.
    virtual bool pinch(int x, int y, float dz)
    {
        (void)x;
        (void)y;
        (void)dz;
        return false;
    }

    /// A second finger landed. Armed taps should be dropped so a pinch is not a click.
    virtual void gesturePinchBegan() {}
};

/// Opens the GLES window. Frame pumps events and presents through this window.
class Screen
{
public:
    /// Creates the window and the GLES context.
    /// Desktop opens a 1024 by 600 window. Android opens fullscreen and takes the display size.
    Screen();

    /// Destroys the context and the window.
    ~Screen();

    /// Applies the current drawable size to the viewport.
    void syncSize();

    /// Clears the framebuffer and applies the current drawable size.
    void beginFrame();

    /// Shows the back buffer.
    void present();

    /// Layout width in pixels. Height and width swap while the layout is vertical.
    int getWidth() const;

    /// Layout height in pixels. Height and width swap while the layout is vertical.
    int getHeight() const;

    /// Drawable width before the layout rotation.
    int physicalWidth() const;

    /// Drawable height before the layout rotation.
    int physicalHeight() const;

    /// Turns the layout and the picture 90 degrees. The window size stays the same.
    static void setLayoutVertical(bool vertical);

    /// True when the layout is the portrait rotation of the window.
    static bool layoutVertical();

    /// Physical SDL point to the layout point widgets use.
    void mapPointer(int x, int y, int &outX, int &outY) const;

private:
    int mWidth;
    int mHeight;
    static bool mLayoutVertical;
    SDL_Window *mWindow = nullptr;
    SDL_GLContext mContext = nullptr;
};

#endif // SCREEN_H
