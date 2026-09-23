/// \file app_controller.h
/// Mode, map, and AIP flags, and the keys that change them.

#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "screen.h"
#include <cstdint>

class AhrsWidget;
class TerrainWidget;
class Frame;

/// Which picture the MODE column is showing.
enum class ViewMode
{
    Ahrs,     ///< Instrument only. Terrain is not drawn.
    ThreeD,   ///< Terrain, map, and the instrument layer without the sky tape.
    TwoD,     ///< Placeholder. Highlights the row and does not change layers.
    Planning, ///< Placeholder. Highlights the row and does not change layers.
};

/// Ground imagery selected from the MAP column.
enum class MapMode
{
    Satellite, ///< Esri imagery draped on the terrain.
    Simple,    ///< Shaded terrain without the satellite drape.
};

/// Page opened from the ENR column. None means the page is closed.
enum class EnrPage
{
    None,
    Charts,     ///< Schematic Europe map row.
    FlightPlan, ///< Placeholder row.
    Nearest,    ///< Speaks the closest field.
    Weather,    ///< Placeholder row.
};

/// Turns layers on for the current mode and handles the mode keys.
class AppController : public IRenderer
{
public:
    /// Applies the default 3D layers and registers for keys ahead of the widgets.
    /// \param frame Loop that offers this object keys first.
    /// \param ahrs Attitude instrument. Must outlive this object.
    /// \param terrain 3D world. Must outlive this object.
    AppController(Frame &frame, AhrsWidget &ahrs, TerrainWidget &terrain);

    /// No drawing. The menu, stats, and settings widgets paint themselves.
    void render() override {}

    /// Mode and map keys. 1 is AHRS, 2 and 3 are 3D, 4 and 5 cycle the map.
    bool keyDown(SDL_Keycode key) override;

    /// Picture currently selected.
    ViewMode view() const { return mView; }
    /// Imagery currently selected.
    MapMode mapMode() const { return mMapMode; }
    /// ENR row currently highlighted.
    EnrPage enrPage() const { return mEnrPage; }
    /// Airspace walls are drawn.
    bool aipWalls() const { return mAipWalls; }
    /// Airspace name plates are drawn.
    bool aipText() const { return mAipText; }
    /// Reporting points are drawn.
    bool vrpsOn() const { return mVrpOn; }
    /// Obstacle masts are drawn.
    bool obstacles() const { return mObstacles; }
    /// The diagnostics panel is open.
    bool statsVisible() const { return mShowStats; }
    /// The GENERAL settings window is open.
    bool generalOpen() const { return mGeneralOpen; }
    /// Close-in imagery zoom.
    int satZoom() const { return mSatZoom; }
    /// Mid-ring imagery zoom.
    int farZoom() const { return mFarZoom; }
    /// Tiles on one side of the mid ring.
    int farGrid() const { return mFarGrid; }
    /// Tiles on one side of the close-in ring.
    int nearGrid() const { return mNearGrid; }
    /// Increases when a menu highlight changes.
    uint64_t revision() const { return mRevision; }

    /// Selects a MODE row. 2D and PLANNING highlight without changing layers.
    void setView(ViewMode mode);
    /// Selects a MAP row. Leaves AHRS-only and returns to 3D.
    void setMap(MapMode mode);
    /// Toggles one AIP row. Row 1 is walls, 2 is text, 3 is reporting points, 4 is obstacles.
    void toggleAip(int row);
    /// Highlights an ENR row. Nearest speaks the closest field.
    void setEnrPage(EnrPage page);
    /// Shows or hides the diagnostics panel.
    void toggleStats();
    /// Opens or closes the GENERAL window.
    void setGeneralOpen(bool open);
    /// Stores the mid-ring zoom and grid and pushes them to the terrain.
    void setFarCoverage(int zoom, int grid);
    /// Stores the close-in zoom and grid and pushes them to the terrain.
    void setNearCoverage(int zoom, int grid);

private:
    void cycleAhrs();
    void cycleMap();
    void applyLayers();
    void touch();

    AhrsWidget &mAhrs;       ///< Attitude instrument. enable() and the sky tape are switched from here.
    TerrainWidget &mTerrain; ///< 3D world. Layer flags and imagery zoom are pushed here.
    ViewMode mView = ViewMode::ThreeD; ///< MODE row. 2D and PLANNING do not change layers.
    MapMode mMapMode = MapMode::Simple; ///< MAP row. Satellite drapes imagery. Simple is shaded terrain.

    bool mAipWalls = false;  ///< AIP 3D. Vertical airspace walls.
    bool mAipText = false;   ///< AIP TEXT. Airspace name plates.
    bool mVrpOn = true;      ///< AIP VRP. Visual reporting points.
    bool mObstacles = false; ///< AIP OBSTCL. Obstacle masts. Voice can still run when this is off.

    int mSatZoom = 16;       ///< Close-in imagery zoom, from 12 through 18.
    int mFarZoom = 12;       ///< Mid-ring imagery zoom, from 9 through 12.
    int mFarGrid = 8;        ///< Tiles on one side of the mid ring. One of 4, 8, 12, 16.
    int mNearGrid = 8;       ///< Tiles on one side of the close-in ring. One of 4, 8, 12, 16.

    bool mGeneralOpen = false; ///< CONF GENERAL window is open.
    EnrPage mEnrPage = EnrPage::None; ///< ENR row. Nearest speaks the closest field when selected.
    
    bool mShowStats = false; ///< CONF STATS. Diagnostics panel instead of the preload banner.
    uint64_t mRevision = 0;  ///< Bumped when a menu highlight changes. The menu rebuilds when this moves.
};

#endif
