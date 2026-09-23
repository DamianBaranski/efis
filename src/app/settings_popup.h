/// \file settings_popup.h
/// GENERAL window: imagery coverage on RENDER and narration on SOUND.

#ifndef SETTINGS_POPUP_H
#define SETTINGS_POPUP_H

#include "europe_map.h"
#include "iwidget.h"
#include "iworld_read.h"
#include "tab_window.h"

class AppController;

/// Settings window opened from CONF GENERAL.
/// RENDER changes the imagery rings. MAPS shows the Europe chart. SOUND drives NavVoice.
class SettingsPopup : public IWidget
{
public:
    /// Builds the tab window and the Europe chart. Draws nothing until GENERAL is open.
    /// \param frame Loop that draws this window on top of the menu.
    /// \param controller Mode flags and the zoom values this window edits.
    /// \param world Camera latitude used to size a ring in kilometres.
    SettingsPopup(Frame &frame, AppController &controller, IWorldRead &world);

    /// Draws the open window. MAPS also draws the Europe chart.
    void render() override;

    /// Unused. The window centres itself.
    void setPos(int, int) override {}

    /// Drops the cached page so the next draw rebuilds the labels.
    void invalidate();

    /// Tab under the point, or -1.
    /// \param x Pixels from the left.
    /// \param y Pixels from the top.
    int hitTab(int x, int y) const;

    /// Page currently showing. 0 is RENDER, 1 is MAPS, 2 is SOUND.
    int activeTab() const;

    /// Switches page and drops the cached labels.
    void setActiveTab(int tab);

    /// Button under the point on the current page, or -1.
    int hitButton(int x, int y) const;

    /// True when the point lies inside the window.
    bool contains(int x, int y) const;

    /// True when the point lies on the Europe chart. MAPS only.
    bool hitChart(int x, int y);

    /// Steps the far or near ring. Controls 0-3 are the far ring, 4-7 the near ring.
    void adjustRender(int control);

    /// Toggles one narration gate, steps the voice, or plays the preview.
    void adjustSound(int control);

private:
    static constexpr double kFarMaxKm = 50.0;
    static constexpr double kNearMaxKm = 10.0;

    static int stepGrid(int grid, int delta);
    static void formatKm(char *out, size_t n, double km);
    double ringRadiusKm(int zoom, int grid) const;
    bool nextCoverage(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int dir) const;
    void nudgeZoom(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int delta) const;
    void drawSoundPage(int screenH);
    void drawPage();

    AppController &mController;
    IWorldRead &mWorld;
    TabWindow<3, 10, 8> mWindow;
    EuropeMap mEuropeMap;
    std::string mKey;
};

#endif
