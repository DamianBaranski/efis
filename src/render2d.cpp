#include "render2d.h"
#include "asset_path.h"
#include <algorithm>
#include <cmath>
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
    const std::string fontPath = AssetPath::resolve("resources/fonts/B612Mono-Regular.ttf");
    TTF_Font *font = TTF_OpenFont(fontPath.c_str(), size);
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

void Render2D::drawTextCentered(std::string text, float size, float x, float y, uint32_t color)
{
    if (TTF_Init() != 0)
    {
        std::cerr << "TTF_Init" << std::endl;
        return;
    }
    const std::string fontPath = AssetPath::resolve("resources/fonts/B612Mono-Regular.ttf");
    TTF_Font *font = TTF_OpenFont(fontPath.c_str(), size);
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

    SDL_Surface *surface = TTF_RenderText_Blended(font, text.c_str(), sdlColor);
    if (!surface)
    {
        std::cerr << "Failed to render text: " << TTF_GetError() << std::endl;
        TTF_CloseFont(font);
        return;
    }
    SDL_Surface *padded =
        SDL_CreateRGBSurfaceWithFormat(0, surface->w + 2, surface->h + 2, 32, SDL_PIXELFORMAT_RGBA32);
    if (padded)
    {
        SDL_FillRect(padded, nullptr, SDL_MapRGBA(padded->format, 0, 0, 0, 0));
        SDL_Rect dst{1, 1, surface->w, surface->h};
        SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
        SDL_BlitSurface(surface, nullptr, padded, &dst);
        SDL_FreeSurface(surface);
        surface = padded;
    }
    const int width = surface->w;
    const int height = surface->h;
    mShader.setTexture(text, surface);
    drawTexture(text, static_cast<int>(x) - width / 2, static_cast<int>(y) - height / 2, width, height);
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
    mShader.clearGeometry();
    mShader.setTriangles(triangles);
}

void Render2D::drawTexture(std::string name, int x, int y)
{
    drawTexture(name, x, y, 1.0f);
}

void Render2D::drawTexture(std::string name, int x, int y, float scale)
{
    int width = mShader.getTextureWidth(name);
    int height = mShader.getTextureHeight(name);
    if (width < 0 || height < 0)
    {
        return;
    }
    const int w = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * scale)));
    drawTexture(name, x - w / 2, y - h / 2, w, h);
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
