#ifndef GEO_COORD_UTILS_H
#define GEO_COORD_UTILS_H

#include <cmath>

/// @brief Utility class for geographic coordinate conversions and calculations.
class GeoCoordUtils
{
public:
    /// @brief Struct to represent Cartesian coordinates (X, Y, Z).
    struct XYZ
    {
        double x; ///< X coordinate.
        double y; ///< Y coordinate.
        double z; ///< Z coordinate.
    };

    /// @brief Struct to represent Euler angles (pitch, yaw, roll).
    struct EulerAngles
    {
        double pitch; ///< Pitch angle.
        double yaw;   ///< Yaw angle.
        double roll;  ///< Roll angle.
    };

    /// @brief Geographic position in degrees.
    struct LatLon
    {
        double latitude;
        double longitude;
    };

    /// @brief Converts latitude, longitude, and altitude to Earth-centered Cartesian coordinates.
    /// @param latitude Latitude in degrees.
    /// @param longitude Longitude in degrees.
    /// @param altitude Altitude in meters (default is 0).
    /// @return XYZ struct containing the Cartesian coordinates.
    static XYZ convertLatLonToXYZ(double latitude, double longitude, double altitude = 0);

    /// Inverse of convertLatLonToXYZ (WGS-84, altitude ignored for texturing).
    static LatLon convertXYZToLatLon(double x, double y, double z);

    /// Web Mercator UV in [0, 1] (Y increases south, matching XYZ map tiles).
    static void latLonToMercatorUv(double latitude, double longitude, float &u, float &v);

    /// Offset a WGS-84 position by north/east metres.
    static LatLon offsetMeters(double latitude, double longitude, double northMeters, double eastMeters);

    /// @brief Generates Euler angles (pitch, yaw, roll) for a ground-level camera given latitude and longitude.
    /// @param latitude Latitude in degrees.
    /// @param longitude Longitude in degrees.
    /// @return EulerAngles struct representing the camera orientation.
    static EulerAngles generateGroundCameraAngles(double latitude, double longitude);

    /// @brief Calculates the great-circle distance between two points on the Earth's surface.
    /// @param lat1 Latitude of the first point in degrees.
    /// @param lon1 Longitude of the first point in degrees.
    /// @param lat2 Latitude of the second point in degrees.
    /// @param lon2 Longitude of the second point in degrees.
    /// @return Distance between the two points in meters.
    static double calculateDistance(double lat1, double lon1, double lat2, double lon2);

private:
    static constexpr double cEarthSemiMajorAxis = 6378137.0;                                                                                        ///< Semi-major axis of the Earth (in meters).
    static constexpr double cInverseFlattening = 298.257223563;                                                                                     ///< Inverse of flattening.
    static constexpr double cFlattening = 1.0 / cInverseFlattening;                                                                                 ///< Flattening.
    static constexpr double cEarthSemiMinorAxis = cEarthSemiMajorAxis * (1.0 - cFlattening);                                                        ///< Semi-minor axis of the Earth.
    static constexpr double cEccentricitySquared = 1.0 - (cEarthSemiMinorAxis * cEarthSemiMinorAxis) / (cEarthSemiMajorAxis * cEarthSemiMajorAxis); ///< Eccentricity squared.

    /// @brief Converts degrees to radians.
    /// @param degrees Angle in degrees.
    /// @return Angle converted to radians.
    static double degreesToRadians(double degrees);

    /// @brief Converts radians to degrees.
    /// @param radians Angle in radians.
    /// @return Angle converted to degrees.
    static double radiansToDegrees(double radians);
};

#endif // GEO_COORD_UTILS_H
