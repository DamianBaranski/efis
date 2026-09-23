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

    /// Window width in pixels.
    int getWidth() const;

    /// Window height in pixels.
    int getHeight() const;

private:
    int mWidth;
    int mHeight;
    SDL_Window *mWindow = nullptr;
    SDL_GLContext mContext = nullptr;
};

#endif // SCREEN_H
