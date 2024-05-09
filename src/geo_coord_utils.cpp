#include "geo_coord_utils.h"

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