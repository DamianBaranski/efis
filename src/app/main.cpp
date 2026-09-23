/// \file main.cpp
/// Builds the window, the situation source, and the 3D and AHRS layers, then runs the frame.
#include "ahrs_widget.h"
#include "app_controller.h"
#include "data_manager_sim.h"
#ifndef EFIS_ANDROID
#include "data_manager_stratux.h"
#endif
#include "frame.h"
#include "menu_widget.h"
#include "screen.h"
#include "sdl_compat.h"
#include "settings_popup.h"
#include "stats_overlay.h"
#include "terrain_widget.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace
{
void printUsage(const char *argv0)
{
    std::cout << "Usage: " << argv0 << " [--sim|--stratux]\n"
              << "  --sim       in-process Stratux simulator (default)\n"
              << "  --stratux   live Stratux at http://127.0.0.1:5000/getSituation\n"
              << "Keys: Tab/1/2/3/4 views, Esc quit\n"
              << "Sim keys: arrows pitch/roll, Q/E heading, W/S speed, +/- alt, R reset\n";
}

/// Steps the simulator from the keys held this frame, and levels the aircraft on R.
class SimInput : public IRenderer
{
public:
    /// \param sim Aircraft the keys drive. Must outlive this object.
    explicit SimInput(DataManagerSim &sim) : mSim(sim) {}

    void render() override {}

    bool keyDown(SDL_Keycode key) override
    {
        if (key == SDLK_r)
        {
            mSim.resetAttitude();
            return true;
        }
        return false;
    }

    /// Advances the aircraft by the time since the previous call.
    void tick()
    {
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

private:
    DataManagerSim &mSim;
    std::chrono::steady_clock::time_point mLastTick{};
    bool mHasClock = false;
};
}

int main(int argc, char **argv)
{
    bool useSim = true;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--sim")
        {
            useSim = true;
        }
        else if (arg == "--stratux")
        {
            useSim = false;
        }
        else if (arg == "--help" || arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    Screen screen(1024, 600);
    Frame frame(screen);

    std::unique_ptr<IDataManager> dataManager;
    std::unique_ptr<SimInput> simInput;
    if (useSim)
    {
        auto simulated = std::make_unique<DataManagerSim>();
        simInput = std::make_unique<SimInput>(*simulated);
        dataManager = std::move(simulated);
        std::cout << "Data source: simulated Stratux" << std::endl;
    }
    else
    {
#ifdef EFIS_ANDROID
        auto simulated = std::make_unique<DataManagerSim>();
        simInput = std::make_unique<SimInput>(*simulated);
        dataManager = std::move(simulated);
        std::cout << "Data source: simulated Stratux (Android)" << std::endl;
#else
        dataManager = std::make_unique<DataManagerStratux>();
        std::cout << "Data source: Stratux HTTP" << std::endl;
#endif
    }

    TerrainWidget terrainWidget(frame, *dataManager);
    AhrsWidget ahrsWidget(frame, *dataManager);
    AppController controller(frame, ahrsWidget, terrainWidget);
    MenuWidget menu(frame, controller);
    StatsOverlay stats(frame, controller, terrainWidget);
    SettingsPopup settings(frame, controller, terrainWidget);
    menu.setPopup(settings);
    if (simInput)
    {
        frame.addInputFront(simInput.get());
        std::cout << "Sim: arrows pitch/roll, Q/E heading, W/S speed, +/- alt, R reset\n";
    }

    frame.setTick([&] {
        terrainWidget.pumpMapPreload();
        if (simInput)
        {
            simInput->tick();
        }
    });

    dataManager->start();
    frame.run();
    return 0;
}
