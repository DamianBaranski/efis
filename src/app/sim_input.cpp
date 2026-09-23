/// \file sim_input.cpp
/// Maps the flight keys onto DataManagerSim.
#include "sim_input.h"
#include "data_manager_sim.h"
#include <chrono>
#include <iostream>

SimInput::SimInput(Frame &frame, DataManagerSim &sim) : mSim(sim)
{
    frame.addInputFront(this);
    std::cout << "Sim: arrows pitch/roll, Q/E heading, W/S speed, +/- alt, R reset\n";
}

bool SimInput::keyDown(SDL_Keycode key)
{
    if (!mEnabled)
    {
        return false;
    }
    if (key == SDLK_r)
    {
        mSim.resetAttitude();
        return true;
    }
    return false;
}

void SimInput::setEnabled(bool enabled)
{
    mEnabled = enabled;
    mHasClock = false;
}

void SimInput::tick()
{
    if (!mEnabled)
    {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    float dt = 0.016f;
    if (mHasClock)
    {
        dt = std::chrono::duration<float>(now - mLastTick).count();
    }
    mLastTick = now;
    mHasClock = true;

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    mSim.tick(dt, keys[SDL_SCANCODE_UP], keys[SDL_SCANCODE_DOWN], keys[SDL_SCANCODE_LEFT], keys[SDL_SCANCODE_RIGHT],
              keys[SDL_SCANCODE_Q], keys[SDL_SCANCODE_E], keys[SDL_SCANCODE_W], keys[SDL_SCANCODE_S],
              keys[SDL_SCANCODE_PAGEUP] || keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS],
              keys[SDL_SCANCODE_PAGEDOWN] || keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]);
}
