/// \file hud.cpp
/// Registers the chrome after the panels exist, so draw order is not construction order.
#include "hud.h"
#include "frame.h"

Hud::Hud(Frame &frame, AppController &controller, IWorldRead &world, ISession &session)
    : mSettings(frame, controller, world, session), mStats(frame, controller, world), mMenu(frame, controller, mSettings)
{
    frame.add(&mMenu);
    frame.add(&mStats);
    frame.add(&mSettings);
}
