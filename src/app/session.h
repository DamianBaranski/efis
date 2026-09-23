/// \file session.h
/// The situation source for one run: simulated flight, or live Stratux on the desktop.

#ifndef SESSION_H
#define SESSION_H

#include <memory>

class Frame;
class IDataManager;
class SimInput;

/// Owns the situation feed and, for the simulator, the flight keys.
struct Session
{
    std::unique_ptr<IDataManager> data;
    std::unique_ptr<SimInput> sim;
};

/// Opens the simulator, or live Stratux when liveStratux is true.
/// Android always opens the simulator. Stratux HTTP is desktop only.
/// \param frame Loop the simulator keys register with.
Session openSession(Frame &frame, bool liveStratux);

#endif
