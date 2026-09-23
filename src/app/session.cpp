/// \file session.cpp
/// Builds the situation feed and switches it between the keyboard, the tablet, and Stratux.
#include "session.h"
#include "data_manager_internal.h"
#include "data_manager_sim.h"
#include "frame.h"
#include "sim_input.h"
#include <iostream>
#ifndef EFIS_ANDROID
#include "data_manager_stratux.h"
#endif

namespace
{
/// Forwards the active feed to the widgets. The widgets stay attached to this object.
class FeedSwitch final : public IDataManager, public IObserver<DataType>
{
public:
    ~FeedSwitch() override { drop(); }

    /// Subscribes to source and publishes its current sample.
    void follow(IDataManager &source)
    {
        if (mSource == &source)
        {
            announce();
            return;
        }
        drop();
        mSource = &source;
        mSource->attach(this, DataType::ATTITUDE_DATA);
        mSource->attach(this, DataType::DYNAMICS_DATA);
        mSource->attach(this, DataType::LOCATION_DATA);
        announce();
    }

    void update(DataType type) override { notify(type); }

    const AttitudeData &getAttitudeData() const override { return mSource->getAttitudeData(); }
    const DynamicsData &getDynamicsData() const override { return mSource->getDynamicsData(); }
    const EngineData &getEngineData() const override { return mSource->getEngineData(); }
    const LocationData &getLocationData() const override { return mSource->getLocationData(); }

private:
    void drop()
    {
        if (mSource == nullptr)
        {
            return;
        }
        mSource->detach(this);
        mSource->detach(this);
        mSource->detach(this);
        mSource->detach(this);
        mSource = nullptr;
    }

    void announce()
    {
        notify(DataType::ATTITUDE_DATA);
        notify(DataType::DYNAMICS_DATA);
        notify(DataType::LOCATION_DATA);
    }

    IDataManager *mSource = nullptr;
};

class FlightSession final : public ISession
{
public:
    FlightSession(Frame &frame, bool liveStratux)
        : mInput(std::make_unique<SimInput>(frame, mAircraft))
    {
#ifndef EFIS_ANDROID
        if (liveStratux)
        {
            mStratux = std::make_unique<DataManagerStratux>();
            mReceiver = true;
            mSource = SituationSource::Stratux;
            mInput->setEnabled(false);
            mBus.follow(*mStratux);
            std::cout << "Data source: Stratux HTTP" << std::endl;
            return;
        }
#else
        (void)liveStratux;
#endif
        mSource = SituationSource::Sim;
        mBus.follow(mAircraft);
#ifdef EFIS_ANDROID
        std::cout << "Data source: simulated Stratux (Android)" << std::endl;
#else
        std::cout << "Data source: simulated Stratux" << std::endl;
#endif
    }

    IDataManager &data() override { return mBus; }

    void start() override
    {
        mAircraft.start();
#ifndef EFIS_ANDROID
        if (mStratux)
        {
            mStratux->start();
        }
#endif
    }

    void tick() override
    {
        if (mSource == SituationSource::Sim)
        {
            mInput->tick();
        }
        else if (mSource == SituationSource::Internal)
        {
            mInternal.tick();
        }
    }

    SituationSource source() const override { return mSource; }

    bool receiver() const override { return mReceiver; }

    SensorReport sensors() const override { return mInternal.report(); }

    bool toggleHorizon() override { return mInternal.toggleHorizon(); }

    bool horizonSet() const override { return mInternal.horizonSet(); }

    bool sensorOn(int index) const override { return mInternal.sensorOn(index); }

    void toggleSensor(int index) override { mInternal.toggleSensor(index); }

    void setSource(SituationSource source) override
    {
        if (source == SituationSource::Stratux && !mReceiver)
        {
            return;
        }
        if (source == mSource)
        {
            return;
        }
        if (source == SituationSource::Internal)
        {
            mInternal.seed(mBus.getAttitudeData(), mBus.getDynamicsData(), mBus.getLocationData());
            mInternal.setRunning(true);
            mInput->setEnabled(false);
            mSource = source;
            mBus.follow(mInternal);
            std::cout << "Data source: tablet sensors" << std::endl;
            return;
        }
        mInternal.setRunning(false);
        if (source == SituationSource::Sim)
        {
            mInput->setEnabled(true);
            mSource = source;
            mBus.follow(mAircraft);
            std::cout << "Data source: keyboard" << std::endl;
            return;
        }
#ifndef EFIS_ANDROID
        mInput->setEnabled(false);
        mSource = SituationSource::Stratux;
        mBus.follow(*mStratux);
        std::cout << "Data source: Stratux HTTP" << std::endl;
#else
        mSource = SituationSource::Sim;
        mInput->setEnabled(true);
        mBus.follow(mAircraft);
#endif
    }

private:
    DataManagerSim mAircraft;
    DataManagerInternal mInternal;
#ifndef EFIS_ANDROID
    std::unique_ptr<DataManagerStratux> mStratux;
#endif
    FeedSwitch mBus;
    std::unique_ptr<SimInput> mInput;
    SituationSource mSource = SituationSource::Sim;
    bool mReceiver = false;
};
}

std::unique_ptr<ISession> openSession(Frame &frame, bool liveStratux)
{
    return std::make_unique<FlightSession>(frame, liveStratux);
}
