/// \file main.cpp
/// Builds the window, the situation source, and the 3D and AHRS layers, then runs the frame.
#include "ahrs_widget.h"
#include "app_controller.h"
#include "frame.h"
#include "hud.h"
#include "screen.h"
#include "session.h"
#include "terrain_widget.h"
#include <iostream>
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
}

int main(int argc, char **argv)
{
    bool liveStratux = false;
    if (const std::optional<int> stop = parseArgs(argc, argv, liveStratux))
    {
        return *stop;
    }

    Screen screen;
    Frame frame(screen);
    const std::unique_ptr<ISession> session = openSession(frame, liveStratux);

    TerrainWidget terrain(frame, session->data());
    AhrsWidget ahrs(frame, session->data());
    frame.add(&terrain);
    frame.add(&ahrs);
    AppController controller(frame, ahrs, terrain);
    Hud hud(frame, controller, terrain);

    frame.setTick([&] { session->tick(); });
    session->start();
    frame.run();
    return 0;
}
