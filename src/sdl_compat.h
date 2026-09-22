#ifndef SDL_COMPAT_H
#define SDL_COMPAT_H

#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif
#else
#include <SDL.h>
#endif

#endif
