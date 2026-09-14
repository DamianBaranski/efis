#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "ahrs_widget.h"
#include "data_manager_sim.h"
#include "screen.h"
#include "terrain_widget.h"
#include <chrono>
#include <iostream>

enum class ViewMode
{
    Combined,
    Ahrs,
    Terrain,
    OpenAip,
};

class AppController : public IRenderer
{
public:
    AppController(Screen &screen, AhrsWidget &ahrs, TerrainWidget &terrain, DataManagerSim *sim)
        : mAhrs(ahrs), mTerrain(terrain), mSim(sim)
    {
        screen.registerController(this);
        applyView();
        std::cout << "Keys: Tab/1/2/3/4 views, F5 airspaces, Esc quit\n";
        if (mSim)
        {
            std::cout << "Sim: arrows pitch/roll, Q/E heading, W/S speed, +/- alt, R reset\n";
        }
    }

    void render() override
    {
        if (!mSim)
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
        mSim->tick(dt,
                   keys[SDL_SCANCODE_UP],
                   keys[SDL_SCANCODE_DOWN],
                   keys[SDL_SCANCODE_LEFT],
                   keys[SDL_SCANCODE_RIGHT],
                   keys[SDL_SCANCODE_Q],
                   keys[SDL_SCANCODE_E],
                   keys[SDL_SCANCODE_W],
                   keys[SDL_SCANCODE_S],
                   keys[SDL_SCANCODE_PAGEUP] || keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS],
                   keys[SDL_SCANCODE_PAGEDOWN] || keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]);
    }

    bool keyDown(SDL_Keycode key) override
    {
        switch (key)
        {
        case SDLK_TAB:
            cycleView();
            return true;
        case SDLK_1:
        case SDLK_F1:
            setView(ViewMode::Ahrs);
            return true;
        case SDLK_2:
        case SDLK_F2:
            setView(ViewMode::Terrain);
            return true;
        case SDLK_3:
        case SDLK_F3:
            setView(ViewMode::Combined);
            return true;
        case SDLK_4:
        case SDLK_F4:
            setView(ViewMode::OpenAip);
            return true;
        case SDLK_5:
        case SDLK_F5:
            mTerrain.setAirspacesEnabled(!mTerrain.airspacesEnabled());
            std::cout << "Airspaces: " << (mTerrain.airspacesEnabled() ? "on" : "off") << std::endl;
            return true;
        case SDLK_r:
            if (mSim)
            {
                mSim->resetAttitude();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

private:
    void cycleView()
    {
        switch (mView)
        {
        case ViewMode::Combined:
            setView(ViewMode::Ahrs);
            break;
        case ViewMode::Ahrs:
            setView(ViewMode::Terrain);
            break;
        case ViewMode::Terrain:
            setView(ViewMode::OpenAip);
            break;
        case ViewMode::OpenAip:
            setView(ViewMode::Combined);
            break;
        }
    }

    void setView(ViewMode view)
    {
        mView = view;
        applyView();
        const char *name = "combined";
        if (view == ViewMode::Ahrs)
        {
            name = "AHRS";
        }
        else if (view == ViewMode::Terrain)
        {
            name = "terrain";
        }
        else if (view == ViewMode::OpenAip)
        {
            name = "OpenAIP";
        }
        std::cout << "View: " << name << std::endl;
    }

    void applyView()
    {
        const bool openAip = (mView == ViewMode::OpenAip);
        const bool terrain = (mView == ViewMode::Combined || mView == ViewMode::Terrain || openAip);
        mTerrain.enable(terrain);
        mTerrain.setOpenAipGround(openAip);
        mAhrs.enable(mView != ViewMode::Terrain);
        mAhrs.setDrawSkyGround(mView == ViewMode::Ahrs);
    }

    AhrsWidget &mAhrs;
    TerrainWidget &mTerrain;
    DataManagerSim *mSim;
    ViewMode mView = ViewMode::Combined;
    std::chrono::steady_clock::time_point mLastTick{};
    bool mHasClock = false;
};

#endif
