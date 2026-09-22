#ifndef EUROPE_BORDERS_H
#define EUROPE_BORDERS_H

/// Simplified European country rings from Natural Earth 110m (public domain).
struct EuropeRing
{
    int vertex0;
    int count;
};

struct EuropeCountry
{
    const char *iso;
    const char *name;
    int ring0;
    int rings;
    int tri0;
    int tris;
};

extern const float kEuropeLonLat[];
extern const unsigned kEuropeIndex[];
extern const EuropeRing kEuropeRings[];
extern const EuropeCountry kEuropeCountries[];
extern const int kEuropeVertexCount;
extern const int kEuropeIndexCount;
extern const int kEuropeRingCount;
extern const int kEuropeCountryCount;
extern const float kEuropeLon0;
extern const float kEuropeLon1;
extern const float kEuropeLat0;
extern const float kEuropeLat1;

#endif
