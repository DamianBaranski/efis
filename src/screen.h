/// \file screen.h
/// \brief Contains the declaration of the Screen class and the IRenderer interface.

#ifndef SCREEN_H
#define SCREEN_H

#include <SDL2/SDL.h>
#include <iostream>
#include <vector>

/// \class IRenderer
/// \brief Interface for objects that can render to the screen.
class IRenderer
{
public:
    /// \brief Renders the object.
    virtual void render() const = 0;

    /// \brief Handles mouse click events.
    /// \param x The x-coordinate of the mouse click.
    /// \param y The y-coordinate of the mouse click.
    /// \return True if the click event is handled, false otherwise.
    virtual bool mouseClick(int x, int y)
    {
        (void)x;
        (void)y;
        return false;
    }
};

/// \class Screen
/// \brief Manages the OpenGL window and rendering.
class Screen
{
public:
    /// \brief Constructs a Screen object with the specified width and height.
    /// \param width The width of the screen.
    /// \param height The height of the screen.
    Screen(int width, int height);

    /// \brief Destroys the Screen object and cleans up resources.
    ~Screen();

    /// \brief Registers a renderer to be rendered on the screen.
    /// \param renderer A pointer to the renderer to be registered.
    void registerRenderer(IRenderer *renderer);

    /// \brief Enters the main event loop of the screen.
    void mainLoop();

    /// \brief Retrieves the width of the screen.
    /// \return The width of the screen.
    int getWidth() const;

    /// \brief Retrieves the height of the screen.
    /// \return The height of the screen.
    int getHeight() const;

private:
    /// \brief Static function wrapper for the display function.
    static void displayWrapper();

    /// \brief Displays the contents of the screen.
    void render();

    int mWidth;                          ///< The width of the screen.
    int mHeight;                         ///< The height of the screen.
    SDL_Window *mWindow = nullptr;       ///< The SDL window associated with the screen.
    SDL_GLContext mContext = nullptr;    ///< The SDL OpenGL context associated with the screen.
    std::vector<IRenderer *> mRenderers; ///< Vector of renderers registered with the screen.
    static Screen *instance;             ///< Pointer to the current instance of Screen.
};

#endif // SCREEN_H
