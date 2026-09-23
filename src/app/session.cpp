/// \file session.cpp
/// Builds a simulator session or a live Stratux session.
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
class SimSession final : public ISession
{
public:
    SimSession(Frame &frame, const char *label)
    {
        mSim = std::make_unique<SimInput>(frame, *mAircraft);
        std::cout << "Data source: " << label << std::endl;
    }

    IDataManager &data() override { return *mAircraft; }

    void start() override { mAircraft->start(); }

    void tick() override { mSim->tick(); }

private:
    std::unique_ptr<DataManagerSim> mAircraft = std::make_unique<DataManagerSim>();
    std::unique_ptr<SimInput> mSim;
};

#ifndef EFIS_ANDROID
class StratuxSession final : public ISession
{
public:
    StratuxSession() { std::cout << "Data source: Stratux HTTP" << std::endl; }

    IDataManager &data() override { return mFeed; }

    void start() override { mFeed.start(); }

    void tick() override {}

private:
    DataManagerStratux mFeed;
};
#endif
}

std::unique_ptr<ISession> openSession(Frame &frame, bool liveStratux)
{
#ifdef EFIS_ANDROID
    (void)liveStratux;
    return std::make_unique<SimSession>(frame, "simulated Stratux (Android)");
#else
    if (!liveStratux)
    {
        return std::make_unique<SimSession>(frame, "simulated Stratux");
    }
    return std::make_unique<StratuxSession>();
#endif
}
