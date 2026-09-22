/// \file geo_coord_utils.cpp
/// Converts WGS-84 and Euler angles into the Earth-centered frame.
#include "geo_coord_utils.h"
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

GeoCoordUtils::XYZ GeoCoordUtils::convertLatLonToXYZ(double latitude, double longitude, double altitude)
{
    latitude = degreesToRadians(latitude);
    longitude = degreesToRadians(longitude);

    double N = cEarthSemiMajorAxis / std::sqrt(1.0 - cEccentricitySquared * std::sin(latitude) * std::sin(latitude));

    double x = (N + altitude) * std::cos(latitude) * std::cos(longitude);
    double y = (N + altitude) * std::cos(latitude) * std::sin(longitude);
    double z = (N * (1.0 - cEccentricitySquared) + altitude) * std::sin(latitude);

    return {x, y, z};
}

GeoCoordUtils::LatLon GeoCoordUtils::convertXYZToLatLon(double x, double y, double z)
{
    const double a = cEarthSemiMajorAxis;
    const double b = cEarthSemiMinorAxis;
    const double e2 = cEccentricitySquared;
    const double ep2 = (a * a - b * b) / (b * b);
    const double p = std::sqrt(x * x + y * y);
    const double theta = std::atan2(z * a, p * b);
    const double sinTheta = std::sin(theta);
    const double cosTheta = std::cos(theta);
    const double lat = std::atan2(z + ep2 * b * sinTheta * sinTheta * sinTheta,
                                  p - e2 * a * cosTheta * cosTheta * cosTheta);
    const double lon = std::atan2(y, x);
    return {radiansToDegrees(lat), radiansToDegrees(lon)};
}

void GeoCoordUtils::latLonToMercatorUv(double latitude, double longitude, float &u, float &v)
{
    const double maxLat = 85.05112878;
    if (latitude > maxLat)
    {
        latitude = maxLat;
    }
    else if (latitude < -maxLat)
    {
        latitude = -maxLat;
    }
    u = static_cast<float>((longitude + 180.0) / 360.0);
    const double latRad = degreesToRadians(latitude);
    v = static_cast<float>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0);
}

GeoCoordUtils::LatLon GeoCoordUtils::offsetMeters(double latitude, double longitude, double northMeters, double eastMeters)
{
    const double metersPerDegLat = 111320.0;
    const double latRad = degreesToRadians(latitude);
    const double metersPerDegLon = std::max(1000.0, metersPerDegLat * std::cos(latRad));
    return {latitude + northMeters / metersPerDegLat, longitude + eastMeters / metersPerDegLon};
}

GeoCoordUtils::EulerAngles GeoCoordUtils::generateGroundCameraAngles(double latitude, double longitude)
{
    latitude = degreesToRadians(latitude);
    longitude = degreesToRadians(longitude);

    double cos_lat = std::cos(latitude);
    double cos_lon = std::cos(longitude);
    double sin_lat = std::sin(latitude);
    double sin_lon = std::sin(longitude);

    // Calculate pitch, yaw, and roll angles
    double pitch = std::atan2(-sin_lat, std::sqrt(cos_lat * cos_lat + sin_lat * sin_lat * sin_lon * sin_lon));
    double yaw = std::atan2(-sin_lon * cos_lat, cos_lon * cos_lat);
    double roll = 0.0; // For a ground-level camera, roll is assumed to be 0

    // Convert angles from radians to degrees
    pitch = radiansToDegrees(pitch);
    yaw = radiansToDegrees(yaw);
    roll = radiansToDegrees(roll);

    return {pitch, yaw, roll};
}

double GeoCoordUtils::degreesToRadians(double degrees)
{
    return degrees * M_PI / 180.0;
}

double GeoCoordUtils::radiansToDegrees(double radians)
{
    return radians * 180.0 / M_PI;
}

double GeoCoordUtils::calculateDistance(double lat1, double lon1, double lat2, double lon2)
{
    // Convert latitude and longitude from degrees to radians
    lat1 = degreesToRadians(lat1);
    lon1 = degreesToRadians(lon1);
    lat2 = degreesToRadians(lat2);
    lon2 = degreesToRadians(lon2);

    // Haversine formula
    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    double distance = c * cEarthSemiMajorAxis;

    return distance;
}