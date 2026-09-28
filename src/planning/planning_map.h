/// \file planning_map.h
/// Planner Map tab: night vector chart or Esri satellite tiles, plus airspaces
/// and close-zoom obstacle markers.

#ifndef PLANNING_MAP_H
#define PLANNING_MAP_H

#include "flight_plan.h"
#include "idata_manager.h"
#include "render2d.h"
#include "screen.h"
#include "shader.h"

#include <cstdint>
#include <string>

/// Airspace filter categories. Mirrors matchesAirspaceFilter in the POC.
enum class AirspaceFilter
{
    All,
    CtrTma,
    Rpd,
    Tra,
    Atz,
};

/// Draws the planner chart from `airspaces.csv`, the enroute waypoint DB, and
/// the plan route. Satellite mode stitches Esri XYZ tiles into the same atlas.
class PlanningMap
{
public:
    /// Binds the map to the same window as its parent widget.
    /// \param screen Window used for GL Y flip and text.
    /// \param data Situation feed for the ownship marker.
    /// \param plan Shared flight plan for the route line.
    PlanningMap(Screen &screen, IDataManager &data, FlightPlan &plan);

    /// Positions the map viewport in SDL coordinates.
    void place(int x, int y, int w, int h);

    /// Centres the map on the route and zooms so the whole plan fills the viewport.
    void fitRoute();

    /// Steps the zoom by `delta`. Clamps to the working range.
    void zoom(int delta);

    /// Steps the zoom by a fractional wheel notch.
    void zoomFine(float delta);

    /// Zooms by `delta` while keeping the geographic point under `(x, y)` fixed.
    void zoomAt(float delta, int x, int y);

    /// Turns airspace polygons on or off.
    void setAirspaceVisible(bool visible)
    {
        if (mShowAirspace != visible)
        {
            mShowAirspace = visible;
            markDirty();
        }
    }
    /// True when airspace polygons are being drawn.
    bool airspaceVisible() const { return mShowAirspace; }

    /// Turns IFR navaids and intersections on or off.
    void setIfrVisible(bool visible)
    {
        if (mShowIfr != visible)
        {
            mShowIfr = visible;
            markDirty();
        }
    }
    /// True when VOR / NDB / intersection points are drawn.
    bool ifrVisible() const { return mShowIfr; }

    /// Sets the airspace category filter.
    void setFilter(AirspaceFilter f)
    {
        if (mFilter != f)
        {
            mFilter = f;
            markDirty();
        }
    }
    /// Currently selected filter.
    AirspaceFilter filter() const { return mFilter; }

    /// Switches between the night vector chart and Esri satellite tiles.
    void toggleBackground();
    /// Selects the night vector chart (`true`) or Esri satellite tiles (`false`).
    void setBackgroundVector(bool vector);
    /// Human-readable background label used by the caller for toast text.
    const char *backgroundLabel() const { return mBackgroundVector ? "VECTOR" : "SATELLITE"; }

    /// Rebuilds the cached mesh on the next draw. Used after an AIP CSV reload.
    void invalidate() { markDirty(); }

    /// True when the point lies inside the map viewport.
    bool contains(int x, int y) const;

    /// Applies a pan drag. `dx`/`dy` are layout pixels.
    void pan(int dx, int dy);

    /// Draws the map. Call from the enclosing widget's render.
    void render();

    /// Latitude at the map centre.
    double centerLat() const { return mCenterLat; }
    /// Longitude at the map centre.
    double centerLon() const { return mCenterLon; }
    /// Zoom in planner units. Pinch and wheel; no floor on zoom-out, max 14.
    float zoomLevel() const { return mZoom; }

    /// Rectangle in SDL pixels.
    int x() const { return mX; }
    int y() const { return mY; }
    int w() const { return mW; }
    int h() const { return mH; }

private:
    struct Point
    {
        float x;
        float y;
    };

    void markDirty() { mDirty = true; }
    void rasterize();
    void pumpSatellite();
    int countCachedSatTiles() const;
    int paintSatellite(SDL_Surface *surface, int texW, int texH, double minLat, double maxLat, double minLon,
                       double maxLon);
    Point project(double lat, double lon) const;
    bool inBox(double minLat, double maxLat, double minLon, double maxLon) const;
    void viewBounds(double &minLat, double &maxLat, double &minLon, double &maxLon) const;
    Point toSurf(double lat, double lon, int texW, int texH, double minLat, double maxLat, double minLon,
                 double maxLon) const;
    void ensureOwnshipArt();
    void drawOwnship(float glX, float glY);

    Screen &mScreen;
    IDataManager &mData;
    FlightPlan &mPlan;

    Shader mUpload;   ///< Uploads the raster atlas into the shared texture cache.
    Render2D mPanel;  ///< One textured quad covering the map viewport.
    Render2D mMarker; ///< Live ownship aircraft symbol.
    Shader mPing;     ///< Expanding range rings around ownship.

    int mX = 0;
    int mY = 0;
    int mW = 0;
    int mH = 0;

    double mCenterLat = 50.6;
    double mCenterLon = 18.3;
    float mZoom = 7.0f;
    bool mShowAirspace = true;
    bool mShowIfr = false;
    bool mBackgroundVector = true;
    AirspaceFilter mFilter = AirspaceFilter::All;
    bool mCentered = false;
    bool mDirty = true;
    std::uint64_t mSeenPlanRevision = 0;
    int mSatZoom = 10;
    int mSatMinX = 0;
    int mSatMaxX = -1;
    int mSatMinY = 0;
    int mSatMaxY = -1;
    int mSatCached = -1;
};

#endif
