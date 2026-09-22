/// \file screen.h
/// GLES window and the ordered list of things drawn each frame.

#ifndef SCREEN_H
#define SCREEN_H

#include "sdl_compat.h"
#include <cstdint>
#include <iostream>
#include <vector>

/// One drawable or input handler in the frame.
/// The controller runs before widgets. Widgets run in registration order.
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

/// Opens the GLES window and runs the SDL loop until quit.
class Screen
{
public:
    /// Creates the window and the GLES context.
    /// \param width Pixels.
    /// \param height Pixels.
    Screen(int width, int height);

    /// Destroys the context and the window.
    ~Screen();

    /// Draws this renderer after the controller, in registration order.
    /// \param renderer Not owned. Must outlive the screen.
    void registerRenderer(IRenderer *renderer);

    /// Runs this object at the start of each frame, before widgets.
    /// \param controller Not owned. Must outlive the screen.
    void registerController(IRenderer *controller);

    /// Pumps events and draws until the window closes.
    void mainLoop();

    /// Window width in pixels.
    int getWidth() const;

    /// Window height in pixels.
    int getHeight() const;

private:
    /// \brief Static function wrapper for the display function.
    static void displayWrapper();

    /// \brief Displays the contents of the screen.
    void render();
    void syncSize();
    bool acceptTouch();

    int mWidth;                          ///< The width of the screen.
    int mHeight;                         ///< The height of the screen.
    SDL_Window *mWindow = nullptr;       ///< The SDL window associated with the screen.
    SDL_GLContext mContext = nullptr;    ///< The SDL OpenGL context associated with the screen.
    std::vector<IRenderer *> mRenderers; ///< Vector of renderers registered with the screen.
    uint64_t mTouchReadyAt = 0;
    uint64_t mLastTouchMs = 0;
    static Screen *instance;             ///< Pointer to the current instance of Screen.
};

#endif // SCREEN_H
