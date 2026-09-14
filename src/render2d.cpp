#include "render2d.h"
#include <iostream>

int Render2D::mPositionZ = 0;

Render2D::Render2D(const Screen &screen): Render2D(screen, mPositionZ++)
{
}

Render2D::Render2D(const Screen &screen, int z): mScreen(screen), mMvp(glm::mat4(1.0))
{
    mMvp[0][0] = 2.0 / mScreen.getWidth();
    mMvp[1][1] = 2.0 / mScreen.getHeight();
    mMvp[3][0] = -1;
    mMvp[3][1] = -1;
    mMvp[3][2] = -z/100.0;
    mShader.setMvpMatrix(mMvp);
}

void Render2D::setRotation(float angle)
{
    mMvp = glm::rotate(mMvp, angle, glm::vec3(0.0f, 0.0f, 1.0f));
    mShader.setMvpMatrix(mMvp);
}

void Render2D::setRotation(float angle, int x, int y)
{
    setPosition(-x, -y);
    mMvp = glm::rotate(mMvp, angle, glm::vec3(0.0f, 0.0f, 1.0f));
    setPosition(x, y);
    mShader.setMvpMatrix(mMvp);
}

void Render2D::setPosition(int x, int y)
{
    mMvp = glm::translate(mMvp, glm::vec3(static_cast<float>(-x),
                                          static_cast<float>(-y), 0));
    mShader.setMvpMatrix(mMvp);
}

void Render2D::setTransformationMatrix(glm::mat4 transform) {
    float z=mMvp[3][2];
    mMvp = glm::mat4(1.0);
    mMvp[0][0] = 2.0 / mScreen.getWidth();
    mMvp[1][1] = 2.0 / mScreen.getHeight();
    mMvp[3][0] = -1;
    mMvp[3][1] = -1;
    mMvp[3][2] = z;
    mMvp = mMvp * transform;
    mShader.setMvpMatrix(mMvp);
}

void Render2D::drawText(std::string text, float size, float x, float y, uint32_t color)
{
    if (TTF_Init() != 0)
    {
        std::cerr << "TTF_Init" << std::endl;
        SDL_Quit();
        return;
    }
    TTF_Font *font = TTF_OpenFont("../resources/fonts/B612Mono-Regular.ttf", size);
    if (!font)
    {
        std::cerr << "Failed to load font: " << TTF_GetError() << std::endl;
        return;
    }

    SDL_Color sdlColor;
    sdlColor.b = (color >> 24) & 0xFF;
    sdlColor.g = (color >> 16) & 0xFF;
    sdlColor.r = (color >> 8) & 0xFF;
    sdlColor.a = (color >> 0) & 0xFF;

    // Create surface from text
    SDL_Surface *surface = TTF_RenderText_Blended(font, text.c_str(), sdlColor);
    if (!surface)
    {
        std::cerr << "Failed to render text: " << TTF_GetError() << std::endl;
        TTF_CloseFont(font);
        return;
    }
    int width = surface->w;
    int height = surface->h;

    mShader.setTexture(text, surface);

    // Draw texture
    drawTexture(text, x, y, width, height);

    // Cleanup
    TTF_CloseFont(font);
}

void Render2D::drawTexture(std::string name, int x, int y, int w, int h)
{
    std::vector<VertexTexture> vertices = {
        {{static_cast<float>(x), static_cast<float>(y), 0}, {0.0f, 1.0f}, {0.0f, 0.0f}},
        {{static_cast<float>(x), static_cast<float>(y + h), 0}, {0.0f, 0.0f}, {0.0f, 0.0f}},
        {{static_cast<float>(x + w), static_cast<float>(y), 0}, {1.0f, 1.0f}, {0.0f, 0.0f}},
        {{static_cast<float>(x + w), static_cast<float>(y + h), 0}, {1.0f, 0.0f}, {0.0f, 0.0f}}};
    std::vector<GLuint> indices = {0, 1, 2, 2, 3, 1};
    std::vector<Triangles> triangles;
    triangles.push_back({
        name,
        vertices,
        indices,
    });
    mShader.setTriangles(triangles);
}

void Render2D::drawTexture(std::string name, int x, int y)
{
    int width = mShader.getTextureWidth(name);
    int height = mShader.getTextureHeight(name);
    if(width < 0 || height < 0) {
        return;
    }

    drawTexture(name, x-width/2, y-height/2, width, height);
}

void Render2D::drawRectangle(int x, int y, int w, int h, uint32_t rgba)
{
    mShader.setColor(std::to_string(rgba), rgba);
    drawTexture(std::to_string(rgba), x, y, w, h);
}

void Render2D::render() const
{
    mShader.render();
}
