/// \file iimagery.h
/// Map tiles pumped around the aircraft and reported as GPU memory.
#ifndef IIMAGERY_H
#define IIMAGERY_H

#include <cstddef>

/// Satellite clipmap or a stitched atlas. The shader binding stays on the concrete type.
class IImagery
{
public:
    virtual ~IImagery() = default;

    /// Upload up to budget tiles around the aircraft.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    virtual void pump(float latitude, float longitude, int budget) = 0;

    /// True when the active tiles are filled.
    virtual bool ready() const = 0;

    /// GPU bytes held by the uploaded tiles.
    virtual size_t gpuBytes() const = 0;
};

#endif
