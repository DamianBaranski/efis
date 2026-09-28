/// \file nav_db.h
/// Airport, enroute waypoint, airspace, and obstacle databases used by the flight planner.

#ifndef NAV_DB_H
#define NAV_DB_H

#include <string>
#include <unordered_map>
#include <vector>

/// One waypoint from `airports.csv` or `enroute_points.csv`.
struct Waypoint
{
    enum class Kind
    {
        Airport, ///< Fixed field, has a runway.
        Fix,     ///< Enroute intersection.
        Vor,     ///< VOR / DME.
        Ndb,     ///< NDB.
        Other,   ///< Anything else.
    };

    std::string ident;      ///< Upper-case identifier.
    std::string name;       ///< Verbose name.
    Kind kind = Kind::Fix;  ///< Type of the point.
    double lat = 0.0;       ///< Latitude in degrees.
    double lon = 0.0;       ///< Longitude in degrees.
    float elevationFt = -1.0f; ///< Field elevation in feet. Negative for unknown.
    std::string country;    ///< Two-letter country code, optional.
    std::string frequency;  ///< Formatted frequency string, optional.
};

/// One OpenAIP obstacle point from the Czech / Polish GeoJSON catalogs.
struct ObstaclePoint
{
    enum class Kind
    {
        Wind,
        Chimney,
        Tower,
        Building,
        Other,
    };

    double lat = 0.0;
    double lon = 0.0;
    float heightM = 0.0f;
    std::string name;
    Kind kind = Kind::Other;
};

/// One row of the airspaces.csv file, keyed by bounding box for cheap culling.
struct AirspaceRing
{
    std::string name;
    std::string type;
    std::string icaoClass;
    std::string floor;
    std::string ceiling;
    double minLat = 90.0;
    double maxLat = -90.0;
    double minLon = 180.0;
    double maxLon = -180.0;
    std::vector<double> lat; ///< Vertex latitudes in degrees.
    std::vector<double> lon; ///< Vertex longitudes in degrees.
};

/// Lazy singleton over the packaged nav databases.
///
/// The first `waypoints()` or `airspaces()` call loads the CSV files under
/// `resources/`. Subsequent calls just return the cached vectors.
class NavDb
{
public:
    /// Global instance. Not thread-safe on construction; call once from the
    /// planner's main-thread lookup before any worker starts.
    static NavDb &instance();

    /// Returns the waypoint with the given identifier, or nullptr. Comparison is
    /// case-insensitive; the ident is upper-cased on load.
    const Waypoint *find(const std::string &ident);

    /// All waypoints. The order is unspecified.
    const std::vector<Waypoint> &waypoints();

    /// Case-insensitive prefix / substring search on ident and name. Returns up
    /// to `limit` matches, prefix-ident hits first, then localeCompare order.
    std::vector<const Waypoint *> search(const std::string &query, size_t limit = 12);

    /// All airspace rings.
    const std::vector<AirspaceRing> &airspaces();

    /// Obstacle points from `resources/obstacles/*.geojson`.
    const std::vector<ObstaclePoint> &obstacles();

    /// Forces a reload of the airspace file. Used by the planner "Load AIP" button.
    void reloadAirspaces();

private:
    NavDb() = default;

    void ensureWaypointsLoaded();
    void ensureAirspacesLoaded();
    void ensureObstaclesLoaded();
    void loadAirportsCsv(const std::string &path);
    void loadEnrouteCsv(const std::string &path);
    void loadAirspacesCsv(const std::string &path);
    void loadObstacleGeoJson(const std::string &path);
    void indexWaypoint(const Waypoint &wpt);

    static std::string upper(std::string s);
    static std::string trim(const std::string &s);

    bool mWaypointsLoaded = false;
    bool mAirspacesLoaded = false;
    bool mObstaclesLoaded = false;
    std::vector<Waypoint> mWaypoints;
    std::unordered_map<std::string, size_t> mIndex;
    std::vector<AirspaceRing> mAirspaces;
    std::vector<ObstaclePoint> mObstacles;
};

#endif
