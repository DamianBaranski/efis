/// \file render2d.h
/// Screen-space text, panels, and sprites. GL origin is the bottom left.
#ifndef RENDER2D_H
#define RENDER2D_H

#include "shader.h"
#include "screen.h"
#include <SDL_ttf.h>

/// Draws HUD text and panels in screen space.
class Render2D
{
public:
    /// Binds drawing to this window. Z is 0.
    /// \param screen Window whose size positions the orthographic view.
    Render2D(const Screen &screen);

    /// @brief Constructs a Render2D object with a specified z-coordinate.
    /// @param screen The screen object.
    /// @param z The z-coordinate.
    Render2D(const Screen &screen, int z);

    /// @brief Sets the rotation angle.
    /// @param angle The rotation angle in radians.
    void setRotation(float angle);

    /// @brief Sets the rotation angle around a specified point.
    /// @param angle The rotation angle in radians.
    /// @param x The x-coordinate of the rotation point.
    /// @param y The y-coordinate of the rotation point.
    void setRotation(float angle, int x, int y);

    /// @brief Sets the position.
    /// @param x The x-coordinate.
    /// @param y The y-coordinate.
    void setPosition(int x, int y);

    /// @brief Sets the transformaion matrix.
    /// @param x The transformation matrix.
    void setTransformationMatrix(glm::mat4 transform);

    /// @brief Draws text on the screen.
    /// @param text The text to be drawn.
    /// @param size The font size.
    /// @param x The x-coordinate of the text position.
    /// @param y The y-coordinate of the text position.
    void drawText(std::string text, float size, float x, float y, uint32_t color=0xFFFFFFFF,
                  const std::string &cacheName = {});

    /// Centered text. `x`,`y` are the center of the baseline box.
    void drawTextCentered(std::string text, float size, float x, float y, uint32_t color = 0xFFFFFFFF,
                          const std::string &cacheName = {});

    /// \brief Draws a texture on the screen.
    /// \param name The name of the texture.
    /// \param x The x-coordinate of the texture center position.
    /// \param y The y-coordinate of the texture center position.
    void drawTexture(std::string name, int x, int y);

    /// Centered draw at `scale` times the texture's native pixel size.
    void drawTexture(std::string name, int x, int y, float scale);

    /// @brief Draws a texture on the screen.
    /// @param name The name of the texture.
    /// @param x The x-coordinate of the texture position.
    /// @param y The y-coordinate of the texture position.
    /// @param w The width of the texture.
    /// @param h The height of the texture.
    void drawTexture(std::string name, int x, int y, int w, int h);

    /// @brief Draws a colored rectangle on the screen.
    /// @param x The x-coordinate of the top-left corner of the rectangle.
    /// @param y The y-coordinate of the top-left corner of the rectangle.
    /// @param w The width of the rectangle.
    /// @param h The height of the rectangle.
    /// @param rgba The color of the rectangle in RGBA format.
    void drawRectangle(int x, int y, int w, int h, uint32_t rgba);

    /// @brief Renders the scene.
    void render() const;

protected:
    const Screen &mScreen; ///< The screen configuration.
    Shader mShader;        ///< The shader used for rendering.
    glm::mat4 mMvp;        ///< The Model-View-Projection matrix.
    static int mPositionZ; ///< The static position z-coordinate.
};

#endif // RENDER2D_H
