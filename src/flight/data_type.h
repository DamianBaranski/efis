/// \file data_type.h
/// Situation blocks published by IDataManager, plus the mesh vertex layout.
#ifndef DATATYPE_H
#define DATATYPE_H

#include <string>
#include <vector>

/// One mesh vertex: Earth-centered position, texture UV, and map UV.
typedef struct
{
    /// \brief Represents the vertex coordinates.
    struct
    {
        float x; ///< Earth-centered metres.
        float y; ///< Earth-centered metres.
        float z; ///< Earth-centered metres.
    } vertex;

    /// \brief Represents the texture coordinates.
    struct
    {
        float x; ///< Texture U.
        float y; ///< Texture V.
    } textureCoord;

    /// Web Mercator UV in [0, 1] for draping map tiles onto the mesh.
    struct
    {
        float x = 0.0f;
        float y = 0.0f;
    } geoCoord;
} VertexTexture;

/// One material and its indexed triangles.
typedef struct
{
    std::string material;                ///< Texture or color name used by the shader.
    std::vector<VertexTexture> vertex;   ///< Vertex buffer.
    std::vector<unsigned int> indices;   ///< Triangle indices into vertex.
} Triangles;

/// Which situation block changed.
enum class DataType
{
    ATTITUDE_DATA, ///< Pitch, roll, and heading.
    DYNAMICS_DATA, ///< Airspeed, vertical speed, and slip.
    ENGINE_DATA,   ///< Engine indications. Unused by the current sources.
    LOCATION_DATA, ///< Latitude, longitude, and altitude.
};

/// Aircraft attitude. Angles are radians.
typedef struct
{
    float roll;    ///< Roll angle [radians].
    float pitch;   ///< Pitch angle [radians].
    float heading; ///< Heading angle [radians].
    /// Body-to-NED quaternion. The 3D view uses this so pitch can pass vertical
    /// without the Euler roll jump that swaps sky and ground.
    bool useQuat = false;
    float qw = 1.0f;
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
} AttitudeData;

/// Speed and slip.
typedef struct
{
    float airspeed;       ///< Metres per second.
    float vertical_speed; ///< Metres per second, up positive.
    float slip_rad;       ///< Slip angle in radians.
} DynamicsData;

/// Engine indications. Current sources leave these at zero.
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

/// WGS-84 position.
typedef struct
{
    float latitude;  ///< Degrees, north positive.
    float longitude; ///< Degrees, east positive.
    float altitude;  ///< Metres above the ellipsoid.
} LocationData;

#endif // DATATYPE_H
