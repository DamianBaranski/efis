/// \file planning_widget.h
/// Full-screen four-tab flight planner opened by MODE -> PLANNING.

#ifndef PLANNING_WIDGET_H
#define PLANNING_WIDGET_H

#include "flight_plan.h"
#include "idata_manager.h"
#include "iwidget.h"
#include "planning_map.h"
#include "render2d.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AppController;
class Frame;
class MenuWidget;

/// Reserved click regions. Each frame, tabs and buttons register a rectangle
/// plus a callback; the click handler walks the list.
struct HitRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    std::function<void()> action;
};

/// Owns the plan, the schematic map, and the four planner tabs. Enabled only
/// when the AppController is in `ViewMode::Planning`.
class PlanningWidget : public IWidget
{
public:
    /// Wires the planner to the loop and the shared state.
    /// \param frame Loop that draws the planner and the input dispatch.
    /// \param controller Mode controller used to leave PLANNING and restore EFIS.
    /// \param data Situation feed used for ownship on the map.
    /// \param plan Shared flight plan. Also read by HSI / route strip.
    PlanningWidget(Frame &frame, AppController &controller, IDataManager &data, FlightPlan &plan);

    /// Points the planner at the layout menu so the two menus do not stay open together.
    /// \param menu Menu owned by Hud. Must outlive this widget.
    void bindMenu(MenuWidget &menu);

    /// Drops an armed map-menu tap when a second finger lands.
    void gesturePinchBegan() override;

    /// Enables / disables the widget. Also toggles SDL text input.
    void enable(bool enable) override;

    /// Draws all four tabs.
    void render() override;

    /// Unused. The widget fills the window.
    void setPos(int, int) override {}

    /// Consumes taps inside the planner. The PLANNING chip returns to the EFIS.
    bool mouseClick(int x, int y) override;

    /// Motion dispatch for the map pan.
    bool mouseMove(int x, int y, int dx, int dy) override;

    /// Release dispatch for the map pan.
    bool mouseUp(int x, int y) override;

    /// Wheel dispatch for the map zoom.
    bool mouseWheel(int x, int y, int dy) override;

    /// Two-finger pinch zoom on the Map tab.
    bool pinch(int x, int y, float dz) override;

    /// Text-input dispatch to the focused route / search field.
    bool textInput(const char *text) override;

    /// Backspace and Enter for the focused text field.
    bool keyDown(SDL_Keycode key) override;

    /// Selects an initial tab. Used by --planning-tab for startup verification.
    /// Values: 0 route, 1 setup, 2 briefing, 3 map. Anything else is ignored.
    void setInitialTab(int tabIndex);

    /// Starts the Map tab on Esri satellite tiles. Used by --planning-sat.
    void setInitialMapSatellite(bool satellite);

private:
    enum class Tab
    {
        Route,
        Setup,
        Briefing,
        Map,
    };

    enum class Focus
    {
        None,
        RouteString,
        Search,
    };

    void showToast(const std::string &text);
    void applyRouteString();
    void parseRouteString(const std::string &str);
    void openSearchModal();
    void closeSearchModal();
    void doWaypointSearch(const std::string &query);
    bool tapIsOnModeChip(int x, int y) const;
    void updateTextInput();
    void loadPreset(const std::vector<std::string> &route);

    void layout();
    void renderSplitMap();
    bool pointingAtSplitMap(int x, int y) const;
    void drawBackground();
    void drawHeader();
    void drawTabStrip();
    void drawToast();
    void drawModal();

    void drawRouteTab();
    void drawSetupTab();
    void drawBriefingTab();
    struct MapChromeRow
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        bool header = false;
        bool selected = false;
        int fontPx = 26;
        std::string label;
        std::function<void()> action;
    };

    struct MapChrome
    {
        int btnX = 0;
        int btnY = 0;
        int btnW = 22;
        int btnH = 56;
        int fitX = 0;
        int fitY = 0;
        int fitW = 0;
        int fitH = 0;
        int panelX = 0;
        int panelY = 0;
        int panelW = 0;
        int panelH = 0;
        std::vector<MapChromeRow> rows;
    };

    bool mapSurfaceActive() const;
    bool menuBlocksMap() const;
    void layoutMapChrome(MapChrome &chrome);
    void drawMapChrome();
    bool handleMapChromeClick(int x, int y);
    void noteMapArm(int x, int y);
    void clearMapArm();
    void drawMapTab();
    void drawMapOverlays();
    void cancelRouteDrag();
    void updateRouteSnap(int x, int y);

    void addRect(int x, int y, int w, int h, std::function<void()> action);
    void drawButton(const std::string &cacheKey, const std::string &label, int x, int y, int w, int h, uint32_t fill,
                    uint32_t textColor, float fontPx, std::function<void()> action);

    void computeTotals(float &totalNm, float &totalMin) const;
    std::vector<LegRow> computeLegRows() const;

    Frame &mFrame;
    AppController &mController;
    MenuWidget *mMenu = nullptr;
    IDataManager &mData;
    FlightPlan &mPlan;

    PlanningMap mMap;

    Tab mTab = Tab::Route;
    bool mModalOpen = false;
    Focus mFocus = Focus::None;
    std::string mRouteBuffer;      ///< Working text of the route string editor.
    std::string mSearchQuery;      ///< Working text of the search box.

    int mScreenW = 0;
    int mScreenH = 0;

    // Toast state.
    std::string mToast;
    std::chrono::steady_clock::time_point mToastUntil{};

    // Frame-scoped drawables. Reallocated on layout changes.
    std::vector<std::unique_ptr<Render2D>> mDraws;
    size_t mDrawCursor = 0;
    Render2D &nextDraw();

    std::vector<HitRect> mHits;

    // Cached search results for the modal.
    std::vector<std::string> mSearchResults;

    // Blink timer for the caret in a focused text field.
    std::chrono::steady_clock::time_point mCaretStart{};

    std::uint64_t mLastRevision = 0;

    bool mSimBriefPending = false;
    std::chrono::steady_clock::time_point mSimBriefAt{};
    bool mMapPinch = false;
    bool mMapMenuOpen = false;
    bool mMapArmScroll = false;
    int mMapScroll = 0;
    int mMapScrollMax = 0;
    bool mSuppressPan = false;
    bool mMapArmed = false;
    int mMapArmX = 0;
    int mMapArmY = 0;
    std::function<void()> mMapArmAction;
    int mMapPaneX = 0;
    int mMapPaneY = 0;
    int mMapPaneW = 0;
    int mMapPaneH = 0;

    bool mEditMode = false;
    int mDragRouteIndex = -1;
    bool mSnapValid = false;
    std::string mSnapIdent;
    double mSnapLat = 0.0;
    double mSnapLon = 0.0;
};

#endif
