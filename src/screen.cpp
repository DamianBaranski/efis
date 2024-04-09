#include "screen.h"

Screen* Screen::instance = nullptr;

Screen::Screen(int width, int height) : mWidth(width), mHeight(height) {
    instance = this;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        exit(1);
    }

    mWindow = SDL_CreateWindow("Screen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
    if (!mWindow) {
        std::cerr << "SDL window creation failed: " << SDL_GetError() << std::endl;
        exit(1);
    }

    mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_ACCELERATED);
    if (!mRenderer) {
        std::cerr << "SDL renderer creation failed: " << SDL_GetError() << std::endl;
        exit(1);
    }
    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "1" );
}

Screen::~Screen() {
    if (mRenderer) {
        SDL_DestroyRenderer(mRenderer);
    }
    if (mWindow) {
        SDL_DestroyWindow(mWindow);
    }
    SDL_Quit();
}

void Screen::registerRenderer(IRenderer* renderer) {
    mRenderers.push_back(renderer);
}

void Screen::update() {
    SDL_RenderPresent(mRenderer);
}

void Screen::mainLoop() {
    bool quit = false;
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }
        }
        render();
    }
}

int Screen::getWidth() const {
    return mWidth;
}

int Screen::getHeight() const {
    return mHeight;
}


void Screen::displayWrapper() {
    if (instance) {
        instance->render();
    }
}

void Screen::render() {
    SDL_SetRenderDrawColor(mRenderer, 0, 0, 0, 255);
    SDL_RenderClear(mRenderer);
    for (auto renderer : mRenderers) {
        renderer->render();
    }
    update();
}
