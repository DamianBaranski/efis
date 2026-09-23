/// \file session.cpp
/// Opens the simulated aircraft or the live Stratux feed.
#include "session.h"
#include "data_manager_sim.h"
#include "frame.h"
#include "sim_input.h"
#include <iostream>
#ifndef EFIS_ANDROID
#include "data_manager_stratux.h"
#endif

namespace
{
Session openSim(Frame &frame, const char *label)
{
    Session session;
    auto aircraft = std::make_unique<DataManagerSim>();
    session.sim = std::make_unique<SimInput>(frame, *aircraft);
    session.data = std::move(aircraft);
    std::cout << "Data source: " << label << std::endl;
    return session;
}
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
