/// \file main.cpp
/// Builds the window, the situation source, and the 3D and AHRS layers, then runs the frame.
#include "ahrs_widget.h"
#include "hsi_widget.h"
#include "traffic_widget.h"
#include "tape_widget.h"
#include "route_strip.h"
#include "app_controller.h"
#include "flight_plan.h"
#include "frame.h"
#include "hud.h"
#include "planning_widget.h"
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
    std::cout << "Usage: " << argv0 << " [--sim|--stratux] [--planning] [--screenshot=PATH]\n"
              << "  --sim       in-process Stratux simulator (default)\n"
              << "  --stratux   live Stratux at http://127.0.0.1:5000/getSituation\n"
              << "  --planning  open MODE -> PLANNING at startup\n"
              << "  --planning-sat  planning Map tab with Esri satellite tiles\n"
              << "  --screenshot[=PATH]  write a PNG after ~1.5s and quit\n"
              << "Keys: Tab/1/2/3/4 views, Esc quit\n"
              << "Sim keys: arrows pitch/roll, Q/E heading, W/S speed, +/- alt, R reset\n";
}

/// \return nullopt to run. Otherwise the process exit code.
std::optional<int> parseArgs(int argc, char **argv, bool &liveStratux, bool &startPlanning, int &planningTab,
                             bool &planningSat, std::string &screenshotPath)
{
    liveStratux = false;
    startPlanning = false;
    planningTab = -1;
    planningSat = false;
    screenshotPath.clear();
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
        else if (arg == "--planning")
        {
            startPlanning = true;
        }
        else if (arg == "--planning-sat")
        {
            startPlanning = true;
            planningTab = 3;
            planningSat = true;
        }
        else if (arg.rfind("--planning-tab=", 0) == 0)
        {
            startPlanning = true;
            planningTab = std::stoi(arg.substr(15));
        }
        else if (arg == "--screenshot")
        {
            screenshotPath = "poc/.tmp/snap-native-map.png";
        }
        else if (arg.rfind("--screenshot=", 0) == 0)
        {
            screenshotPath = arg.substr(13);
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
    bool startPlanning = false;
    int planningTab = -1;
    bool planningSat = false;
    std::string screenshotPath;
    if (const std::optional<int> stop = parseArgs(argc, argv, liveStratux, startPlanning, planningTab, planningSat,
                                                    screenshotPath))
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

    // Shared flight plan. Consumed by the HSI, the route strip, and the planner.
    FlightPlan flightPlan;

    // Instruments read the situation feed. They are not on the draw list yet.
    TerrainWidget terrain(frame, session->data());
    AhrsWidget ahrs(frame, session->data());

    // Draw order: 3D world, attitude instrument, the strip under it, then the side dials.
    frame.add(&terrain);
    frame.add(&ahrs);
    RouteStrip route(frame, session->data());
    route.setFlightPlan(&flightPlan);
    frame.add(&route);
    HsiWidget hsiLeftBottom(frame, session->data(), HsiWidget::Slot::LeftBottom);
    HsiWidget hsiLeftTop(frame, session->data(), HsiWidget::Slot::LeftTop);
    HsiWidget hsiRightBottom(frame, session->data(), HsiWidget::Slot::RightBottom);
    hsiLeftBottom.setFlightPlan(&flightPlan);
    hsiLeftTop.setFlightPlan(&flightPlan);
    hsiRightBottom.setFlightPlan(&flightPlan);
    TrafficWidget trafficRightTop(frame, session->data(), TrafficWidget::Slot::RightTop);
    frame.add(&hsiLeftBottom);
    frame.add(&hsiLeftTop);
    frame.add(&hsiRightBottom);
    frame.add(&trafficRightTop);
    TapeWidget tapes(frame, session->data());
    frame.add(&tapes);

    // Mode keys and layer switches. Holds the real widgets because the frame list is only IRenderer.
    // Registers for keys ahead of the widgets. Does not draw.
    AppController controller(frame, ahrs, terrain);
    controller.addCockpitLayer(&route);
    controller.addCockpitLayer(&hsiLeftBottom);
    controller.addCockpitLayer(&hsiLeftTop);
    controller.addCockpitLayer(&hsiRightBottom);
    controller.addCockpitLayer(&trafficRightTop);
    controller.addCockpitLayer(&tapes);

    // Full-screen flight planner. Added before the HUD. The top menu is hidden
    // in PLANNING; the header chip is the only way back to the EFIS.
    PlanningWidget planner(frame, controller, session->data(), flightPlan);
    frame.add(&planner);
    controller.setPlanningWidget(&planner);

    // Menu, stats, and GENERAL. Added above the instruments and the planner.
    // SOURCES on GENERAL switches the feed the instruments already read.
    Hud hud(frame, controller, terrain, *session);
    planner.bindMenu(hud.menu());
    frame.addInputFront(&planner);
    if (startPlanning)
    {
        planner.setInitialTab(planningTab);
        if (planningSat)
        {
            planner.setInitialMapSatellite(true);
        }
        controller.setView(ViewMode::Planning);
    }
    if (!screenshotPath.empty())
    {
        frame.setScreenshot(screenshotPath, planningSat ? 5000u : 1500u);
    }

    // Each frame, before drawing: step the simulator. Stratux does nothing here.
    frame.setTick([&] { session->tick(); });
    // Park the simulator at home, or start the Stratux HTTP poll.
    session->start();
    // Pump events, tick, draw, present, until the window closes.
    frame.run();
    return 0;
}
