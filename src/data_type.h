/// \file datatype.h
/// \brief Contains the declaration of the DataType enumeration and related data structures.
#ifndef DATATYPE_H
#define DATATYPE_H

#include <string>
#include <vector>

/// \struct VertexTexture
/// \brief Represents a vertex with associated texture coordinates.
typedef struct
{
    /// \brief Represents the vertex coordinates.
    struct
    {
        float x; ///< X-coordinate of the vertex.
        float y; ///< Y-coordinate of the vertex.
        float z; ///< Z-coordinate of the vertex.
    } vertex;

    /// \brief Represents the texture coordinates.
    struct
    {
        float x; ///< X-coordinate of the texture coordinate.
        float y; ///< Y-coordinate of the texture coordinate.
    } textureCoord;
} VertexTexture;

/// \struct Triangles
/// \brief Represents a set of triangles with associated material, vertices, and indices.
typedef struct
{
    std::string material;                ///< Material of the triangle.
    std::vector<VertexTexture> vertex;   ///< Vertices of the triangle.
    std::vector<unsigned short> indices; ///< Indices of the triangle.
} Triangles;

/// \enum DataType
/// \brief Enumerates different types of data.
enum class DataType
{
    ATTITUDE_DATA, ///< Attitude data type.
    DYNAMICS_DATA, ///< Dynamics data type.
    ENGINE_DATA,   ///< Engine data type.
    LOCATION_DATA, ///< Location data type.
};

/// \struct AttitudeData
/// \brief Represents attitude data.
typedef struct
{
    float roll;    ///< Roll angle.
    float pitch;   ///< Pitch angle.
    float heading; ///< Heading angle.
} AttitudeData;

/// \struct DynamicsData
/// \brief Represents dynamics data.
typedef struct
{
    float airspeed;       ///< Airspeed.
    float vertical_speed; ///< Vertical speed.
    float slip_rad;       ///< Slip angle in radians.
} DynamicsData;

/// \struct EngineData
/// \brief Represents engine data.
typedef struct
{
    float rpm;       ///< Engine RPM.
    float fuel_flow; ///< Fuel flow rate.
    float fuel_px;   ///< Fuel pressure.
    float oil_temp;  ///< Oil temperature.
    float oil_press; ///< Oil pressure.
    float cht;       ///< Cylinder head temperature.
    float fuel_qty;  ///< Fuel quantity.
} EngineData;

/// \struct LocationData
/// \brief Represents location data.
typedef struct
{
    float latitude;  ///< Latitude.
    float longitude; ///< Longitude.
    float altitude;  ///< Altitude.
} LocationData;

#endif // DATATYPE_H
