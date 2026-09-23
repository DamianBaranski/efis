/// \file hud.h
/// Menu, stats, and the GENERAL window, drawn in that order.

#ifndef HUD_H
#define HUD_H

#include "menu_widget.h"
#include "settings_popup.h"
#include "stats_overlay.h"

class AppController;
class Frame;
class IWorldRead;

/// Chrome over the instruments. Owns the menu, the diagnostics, and GENERAL.
class Hud
{
public:
    /// Builds the panels and adds them to the frame: menu, stats, settings on top.
    /// \param frame Loop that draws the chrome after the instruments.
    /// \param controller Mode flags and imagery coverage.
    /// \param world Camera and preload measurements.
    Hud(Frame &frame, AppController &controller, IWorldRead &world);

private:
    SettingsPopup mSettings;
    StatsOverlay mStats;
    MenuWidget mMenu;
};

#endif
