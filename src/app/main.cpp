/// \file main.cpp
/// Builds the window, the situation source, and the 3D and AHRS layers, then runs the frame.
#include "ahrs_widget.h"
#include "app_controller.h"
#include "data_manager_sim.h"
#ifndef EFIS_ANDROID
#include "data_manager_stratux.h"
#endif
#include "screen.h"
#include "sdl_compat.h"
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

    std::unique_ptr<IDataManager> dataManager;
    DataManagerSim *sim = nullptr;
    if (useSim)
    {
        auto simulated = std::make_unique<DataManagerSim>();
        sim = simulated.get();
        dataManager = std::move(simulated);
        std::cout << "Data source: simulated Stratux" << std::endl;
    }
    else
    {
#ifdef EFIS_ANDROID
        auto simulated = std::make_unique<DataManagerSim>();
        sim = simulated.get();
        dataManager = std::move(simulated);
        std::cout << "Data source: simulated Stratux (Android)" << std::endl;
#else
        dataManager = std::make_unique<DataManagerStratux>();
        std::cout << "Data source: Stratux HTTP" << std::endl;
#endif
    }

    TerrainWidget terrainWidget(screen, *dataManager);
    AhrsWidget ahrsWidget(screen, *dataManager);
    AppController controller(screen, ahrsWidget, terrainWidget, sim);

    dataManager->start();
    screen.mainLoop();
    return 0;
}
