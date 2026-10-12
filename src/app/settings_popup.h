/// \file settings_popup.h
/// GENERAL window: a page list, described rows, and softkeys for the cursor row.

#ifndef SETTINGS_POPUP_H
#define SETTINGS_POPUP_H

#include "europe_map.h"
#include "iwidget.h"
#include "render2d.h"
#include "iworld_read.h"
#include "session.h"
#include "tab_window.h"

#include <string>
#include <vector>

class AppController;
class StatsOverlay;

/// Settings window opened from the flight menu.
/// A page list names Imagery, Maps, Sound, Situation, Display, and Debug.
/// The cursor sits on one row. The softkeys along the bottom belong to that row.
class SettingsPopup : public IWidget
{
public:
    /// What a tap landed on. Index is a page, a row id, or a softkey slot.
    enum class TapKind
    {
        None,
        Tab,
        Row,
        Key,
        Chart,
    };

    /// Builds the page list and the Europe chart. Draws nothing until GENERAL is open.
    /// \param frame Loop that draws this window on top of the instruments.
    /// \param controller Mode flags and the zoom values this window edits.
    /// \param world Camera latitude used to size a ring in kilometres.
    /// \param session Situation feed the Situation page switches.
    SettingsPopup(Frame &frame, AppController &controller, IWorldRead &world, ISession &session);

    /// Draws the open window. Maps also draws the Europe chart. Debug draws the diagnostics.
    void render() override;

    /// Unused. The window fills the screen.
    void setPos(int, int) override {}

    /// Drops the cached page so the next draw rebuilds the labels.
    void invalidate();

    /// Diagnostics drawn on the Debug page. Called once the overlay exists.
    void bindDebug(StatsOverlay &stats);

    /// Page, row, softkey, or chart under the point.
    /// \param x Pixels from the left.
    /// \param y Pixels from the top.
    TapKind hit(int x, int y, int &index);

    /// Runs the armed tap. A chart tap uses the point. A row tap moves the cursor.
    void activate(TapKind kind, int index, int x, int y);

    /// Closes the voice list and returns to Sound.
    /// \return True when a lower page was open. False when GENERAL itself should close.
    bool handleBack();

    /// Moves the voice list. Positive dy shows earlier voices.
    /// \return True while the voice list is open.
    bool scrollBy(int dy);

    /// True when the point lies on the Europe chart.
    bool hitChart(int x, int y);

private:
    static constexpr double kFarMaxKm = 50.0;
    static constexpr double kNearMaxKm = 10.0;
    static constexpr int kPages = 6;
    static constexpr int kMaxRows = 8;

    enum class Act
    {
        None = 0,
        FarCloser,
        FarFarther,
        FarZoomOut,
        FarZoomIn,
        NearCloser,
        NearFarther,
        NearZoomOut,
        NearZoomIn,
        NarrOn,
        NarrOff,
        AirOn,
        AirOff,
        PointOn,
        PointOff,
        NearOn,
        NearOff,
        ObstOn,
        ObstOff,
        Play,
        OpenVoices,
        UseVoice,
        SourceSim,
        SourceTab,
        SourceRadio,
        SensOn,
        SensOff,
        Horizon,
        LayHoriz,
        LayVert,
    };

    struct Line
    {
        int id = 0;
        std::string name;
        std::string sentence;
        std::string value;
        bool dim = false;
        bool cursor = false;
        bool marked = false;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct Soft
    {
        std::string label;
        bool marked = false;
        Act action = Act::None;
        int arg = 0;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    static int stepGrid(int grid, int delta);
    static void formatKm(char *out, size_t n, double km);
    double ringRadiusKm(int zoom, int grid) const;
    bool nextCoverage(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int dir) const;
    void nudgeZoom(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int delta) const;
    void adjustRender(int control);
    void layoutBody();
    void drawBody(int screenH);
    void refreshLive(int screenH);
    void apply(const Soft &key);
    void addSoft(const char *label, bool marked, Act action, int arg = 0);

    AppController &mController;
    IWorldRead &mWorld;
    ISession &mSession;
    StatsOverlay *mStats = nullptr;
    TabWindow<kPages, 24, 6> mWindow;
    EuropeMap mEuropeMap;
    Render2D mCoverageFill;
    std::string mKey;
    std::string mTitle;
    std::string mSensorValue[4];
    std::vector<Line> mLines;
    std::vector<Soft> mSoft;
    int mCursor[kPages] = {};
    bool mVoicePage = false;
    int mVoiceCursor = 0;
    int mVoiceScroll = 0;
    int mVoiceMax = 0;
    int mVoiceDrag = 0;
    int mChartX = 0;
    int mChartY = 0;
    int mChartW = 0;
    int mChartH = 0;
};

#endif
