/// \file screen.h
/// \brief Contains the declaration of the Screen class and the IRenderer interface.

#ifndef SCREEN_H
#define SCREEN_H

#include <SDL2/SDL.h>
#include <iostream>
#include <vector>

/// \class IRenderer
/// \brief Interface for objects that can render to the screen.
class IRenderer {
public:
    /// \brief Renders the object.
    virtual void render() const = 0;
};

/// \class Screen
/// \brief Manages the OpenGL window and rendering.
class Screen {
public:
    /// \brief Constructs a Screen object with the specified width and height.
    /// \param width The width of the screen.
    /// \param height The height of the screen.
    Screen(int width, int height);

    ~Screen();


    /// \brief Registers a renderer to be rendered on the screen.
    /// \param renderer A pointer to the renderer to be registered.
    void registerRenderer(IRenderer *renderer);

    /// \brief Updates the screen, triggering a redraw.
    void update();

    /// \brief Enters the main event loop of the screen.
    void mainLoop();
 
    int getWidth() const;

    int getHeight() const;
    
SDL_Renderer* getRenderer() {
    return mRenderer;
}
private:
    /// \brief Static function wrapper for the display function.
    static void displayWrapper();

    /// \brief Displays the contents of the screen.
    void render();
    int mWidth;
    int mHeight;
    SDL_Window* mWindow = nullptr;
    SDL_Renderer* mRenderer = nullptr;
    std::vector<IRenderer *> mRenderers;  ///< Vector of renderers registered with the screen.
    static Screen *instance;               ///< Pointer to the current instance of Screen.
};

#endif // SCREEN_H
