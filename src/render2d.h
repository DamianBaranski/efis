#ifndef RENDER2D_H
#define RENDER2D_H
#include "shader.h"
#include <SDL_ttf.h>

class Render2D {
    public:
    Render2D() {
        mShader.setMvpMatrix(glm::mat4(1));
    }

    void drawText(std::string text, float size, float x, float y) {
        // Load font
        TTF_Init();
        TTF_Font* font = TTF_OpenFont("../resources/fonts/Amatic.ttf", size);
        if (!font) {
            std::cerr << "Failed to load font: " << TTF_GetError() << std::endl;
            return;
        }

        // Create surface from text
        SDL_Color color = {0, 255, 255, 255}; // White color with full opacity
        SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), color);
        if (!surface) {
            std::cerr << "Failed to render text: " << TTF_GetError() << std::endl;
            TTF_CloseFont(font);
            return;
        }

        std::cout << "Surface w:" << surface->w << " h:" << surface->h << std::endl;
        mShader.setTexture(text, surface);

        // Free surface
        SDL_FreeSurface(surface);

        // Draw texture
        drawTexture(text, x, y, surface->w/40.0, surface->h/40.0);

        // Cleanup
        TTF_CloseFont(font);
    }

    void drawTexture(std::string name, float x, float y, float w, float h) {
        std::vector<VertexTexture> vertices = {
            {{x, y, 0}, {0.0f, 1.0f}},
            {{x, y+h, 0}, {0.0f, 0.0f}},
            {{x+w, y, 0}, {1.0f, 1.0f}},
            {{x+w, y+h, 0}, {1.0f, 0.0f}}
        };
        std::vector<GLushort> indices = {0,1,2,2,3,1};
        std::vector<Triangles> triangles;
        triangles.push_back({
            name,
            vertices,
            indices,
        });
        mShader.setTriangles(triangles);
    }
    
    void render() const {
        mShader.render();
    }

    protected:
    Shader mShader;
};

#endif