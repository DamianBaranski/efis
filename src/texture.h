#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <iostream>

class Texture {
public:
    Texture() : mTexture(nullptr), mWidth(0), mHeight(0) {}

    ~Texture() {
        free();
    }

    bool load(const std::string& filename, SDL_Renderer* renderer) {
        SDL_Surface* surface = IMG_Load(filename.c_str());
        if (!surface) {
            std::cerr << "Error loading image: " << filename << " - " << IMG_GetError() << std::endl;
            return false;
        }

        mTexture = SDL_CreateTextureFromSurface(renderer, surface);
        if (!mTexture) {
            std::cerr << "Error creating texture: " << filename << " - " << SDL_GetError() << std::endl;
            SDL_FreeSurface(surface);
            return false;
        }

        mWidth = surface->w;
        mHeight = surface->h;

        SDL_FreeSurface(surface);

        return true;
    }

    void draw(SDL_Renderer* renderer, float x, float y, float width, float height) const {
        SDL_Rect destRect = { static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), static_cast<int>(height) };
        SDL_RenderCopyEx(renderer, mTexture, nullptr, &destRect, 0,nullptr,SDL_RendererFlip::SDL_FLIP_NONE);
    }

    int getWidth() const {
        return mWidth;
    }

    int getHeight() const {
        return mHeight;
    }

private:
    void free() {
        if (mTexture) {
            SDL_DestroyTexture(mTexture);
            mTexture = nullptr;
        }
    }

    SDL_Texture* mTexture;
    int mWidth, mHeight;
};
