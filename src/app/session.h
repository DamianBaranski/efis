/// \file session.h
/// One run's situation feed: simulated flight, or live Stratux on the desktop.

#ifndef SESSION_H
#define SESSION_H

#include "idata_manager.h"
#include <memory>

class Frame;

/// Starts and steps the situation feed for one run.
class ISession
{
public:
    virtual ~ISession() = default;

    /// Attitude, dynamics, engine, and position. Outlives the widgets that read it.
    virtual IDataManager &data() = 0;

    /// Starts the feed. The simulator parks at home. Stratux opens the HTTP poll.
    virtual void start() = 0;

    /// Steps the feed for this frame. Live Stratux does nothing here.
    virtual void tick() = 0;
};

/// Opens the simulator, or live Stratux when liveStratux is true.
/// Android always opens the simulator. Stratux HTTP is desktop only.
/// \param frame Loop the simulator keys register with.
std::unique_ptr<ISession> openSession(Frame &frame, bool liveStratux);

#endif
