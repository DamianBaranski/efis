/// \file sim_input.h
/// Keyboard flight for the in-process simulator.

#ifndef SIM_INPUT_H
#define SIM_INPUT_H

#include "frame.h"
#include <chrono>

class DataManagerSim;

/// Holds the flight keys and steps the simulator once per frame.
class SimInput : public IRenderer
{
public:
    /// Registers for the R key ahead of the other handlers.
    /// \param frame Loop that offers this object keys. Must outlive it.
    /// \param sim Aircraft the keys drive. Must outlive this object.
    SimInput(Frame &frame, DataManagerSim &sim);

    /// No drawing. The frame tick calls tick().
    void render() override {}

    /// Levels the aircraft when R is pressed.
    bool keyDown(SDL_Keycode key) override;

    /// Advances the aircraft from the keys held since the previous call.
    /// Does nothing while the keyboard feed is not selected.
    void tick();

    /// Turns the flight keys on or off. Off leaves the aircraft where it is.
    void setEnabled(bool enabled);

private:
    DataManagerSim &mSim;
    std::chrono::steady_clock::time_point mLastTick{};
    bool mHasClock = false;
    bool mEnabled = true;
};

#endif
