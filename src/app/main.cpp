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
    // --sim is the default. --stratux asks for the live receiver. Help and bad args exit here.
    bool liveStratux = false;
    if (const std::optional<int> stop = parseArgs(argc, argv, liveStratux))
    {
        return *stop;
    }

    // Desktop opens 1024x600. Android opens fullscreen and takes the display size.
    Screen screen;
    // Event loop and the draw list. Nothing is drawn until it is added below.
    Frame frame(screen);

    // Simulator, or Stratux HTTP on the desktop. Android always gets the simulator.
    // The simulator also registers its flight keys on the frame.
    const std::unique_ptr<ISession> session = openSession(frame, liveStratux);

    // Instruments read the situation feed. They are not on the draw list yet.
    TerrainWidget terrain(frame, session->data());
    AhrsWidget ahrs(frame, session->data());

    // Draw order: 3D world, then the attitude instrument on top of it.
    frame.add(&terrain);
    frame.add(&ahrs);

    // Mode keys and layer switches. Holds the real widgets because the frame list is only IRenderer.
    // Registers for keys ahead of the widgets. Does not draw.
    AppController controller(frame, ahrs, terrain);
    
    // Menu, stats, and GENERAL. Added above the instruments, settings window last.
    Hud hud(frame, controller, terrain);

    // Each frame, before drawing: step the simulator. Stratux does nothing here.
    frame.setTick([&] { session->tick(); });
    // Park the simulator at home, or start the Stratux HTTP poll.
    session->start();
    // Pump events, tick, draw, present, until the window closes.
    frame.run();
    return 0;
}
