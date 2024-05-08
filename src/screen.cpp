#include "screen.h"
#include "GLES3/gl3.h"

Screen *Screen::instance = nullptr;

Screen::Screen(int width, int height) : mWidth(width), mHeight(height)
{
    instance = this;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // Enable anti-aliasing
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4); // Adjust the sample count as needed

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        exit(1);
    }

    mWindow = SDL_CreateWindow("Screen", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!mWindow)
    {
        std::cerr << "SDL window creation failed: " << SDL_GetError() << std::endl;
        exit(1);
    }

    mContext = SDL_GL_CreateContext(mWindow);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    SDL_GL_SwapWindow(mWindow);
}

Screen::~Screen()
{
    if (mWindow)
    {
        SDL_DestroyWindow(mWindow);
    }
    SDL_Quit();
}

void Screen::registerRenderer(IRenderer *renderer)
{
    mRenderers.push_back(renderer);
}

void Screen::mainLoop()
{
    int i = 0;
    uint64_t start = SDL_GetTicks64();
    bool quit = false;
    while (!quit)
    {
        i++;
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                quit = true;
                break;

            case SDL_MOUSEBUTTONDOWN:
                for (auto renderer : mRenderers)
                {
                    if (renderer->mouseClick(event.button.x, event.button.y))
                    {
                        break;
                    }
                }
                break;
            }
        }
        render();
        if ((SDL_GetTicks64() - start) >= 1000)
        {
            std::cout << i << "FPS" << std::endl;
            i = 0;
            start = SDL_GetTicks64();
        }
    }
}

int Screen::getWidth() const
{
    return mWidth;
}

int Screen::getHeight() const
{
    return mHeight;
}

void Screen::displayWrapper()
{
    if (instance)
    {
        instance->render();
    }
}

void Screen::render()
{
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (auto renderer : mRenderers)
    {
        renderer->render();
    }

    SDL_GL_SwapWindow(mWindow);
}
