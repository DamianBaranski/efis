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
#include "settings_popup.h"
#include "sim_input.h"
#include "stats_overlay.h"
#include "terrain_widget.h"
#include <iostream>
#include <memory>
#include <optional>
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

/// \return nullopt to run. Otherwise the process exit code.
std::optional<int> parseArgs(int argc, char **argv, bool &liveStratux)
{
    liveStratux = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--sim")
        {
            liveStratux = false;
        }
        else if (arg == "--stratux")
        {
            liveStratux = true;
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
    return std::nullopt;
}

struct Session
{
    std::unique_ptr<IDataManager> data;
    std::unique_ptr<SimInput> sim;
};

Session openSim(Frame &frame, const char *label)
{
    Session session;
    auto aircraft = std::make_unique<DataManagerSim>();
    session.sim = std::make_unique<SimInput>(frame, *aircraft);
    session.data = std::move(aircraft);
    std::cout << "Data source: " << label << std::endl;
    return session;
}

Session openSession(Frame &frame, bool liveStratux)
{
#ifdef EFIS_ANDROID
    (void)liveStratux;
    return openSim(frame, "simulated Stratux (Android)");
#else
    if (!liveStratux)
    {
        return openSim(frame, "simulated Stratux");
    }
    Session session;
    session.data = std::make_unique<DataManagerStratux>();
    std::cout << "Data source: Stratux HTTP" << std::endl;
    return session;
#endif
}
}

int main(int argc, char **argv)
{
    bool liveStratux = false;
    if (const std::optional<int> stop = parseArgs(argc, argv, liveStratux))
    {
        return *stop;
    }

    Screen screen(1024, 600);
    Frame frame(screen);
    Session session = openSession(frame, liveStratux);

    TerrainWidget terrain(frame, *session.data);
    AhrsWidget ahrs(frame, *session.data);
    AppController controller(frame, ahrs, terrain);
    MenuWidget menu(frame, controller);
    StatsOverlay stats(frame, controller, terrain);
    SettingsPopup settings(frame, controller, terrain);
    menu.setPopup(settings);

    if (session.sim)
    {
        frame.setTick([&] { session.sim->tick(); });
    }

    session.data->start();
    frame.run();
    return 0;
}
