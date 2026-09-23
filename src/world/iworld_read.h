/// \file iworld_read.h
/// Read-only view of the 3D world for the stats panel and the settings window.

#ifndef IWORLD_READ_H
#define IWORLD_READ_H

#include "sat_clipmap.h"

/// Measurements the chrome reads. Layer switches stay on AppController.
class IWorldRead
{
public:
    virtual ~IWorldRead() = default;

    /// Camera latitude in degrees, north positive.
    virtual float cameraLatitude() const = 0;
    /// Camera altitude in metres.
    virtual float cameraAltitude() const = 0;
    /// True when the satellite rings are filled, or when no imagery is requested.
    virtual bool mapPreloadReady() const = 0;
    /// Tiles finished in the close-in ring.
    virtual SatClipmap::Progress nearPreload() const = 0;
    /// Tiles finished in the mid ring.
    virtual SatClipmap::Progress farPreload() const = 0;
    /// GPU bytes held by the satellite rings.
    virtual size_t mapGpuBytes() const = 0;
    /// Terrain tiles submitted on the last frame.
    virtual int terrainDrawn() const = 0;
    /// Terrain tiles held in memory.
    virtual int terrainLoaded() const = 0;
};

#endif
