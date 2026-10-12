/// \file general_settings.h
/// GENERAL setup. Android opens a platform settings list. Desktop opens the drawn window.
#ifndef GENERAL_SETTINGS_H
#define GENERAL_SETTINGS_H

class AppController;
class ISession;
class IWorldRead;
class SettingsPopup;

/// Remembers the engine objects the settings list reads and writes.
/// Call once after the session, terrain, and settings window exist.
void bindGeneralSettings(AppController &controller, ISession &session, IWorldRead &world, SettingsPopup &popup);

/// Opens GENERAL. Android shows the platform list. Desktop shows the drawn window.
void openGeneralSettings();

/// Writes the current GENERAL values to settings.json.
void saveGeneralSettings();

/// Applies queued edits on the frame thread. Call once per frame before the session tick.
void generalSettingsTick();

/// True while the Europe coverage chart is showing beside the settings list.
bool coverageMapOpen();

/// Rectangle of the chart, in pixels from the top left. Width 0 means the full screen.
void coverageMapRect(int &x, int &y, int &w, int &h);

/// Closes the Europe coverage chart.
void closeCoverageMap();

#endif
