#ifndef OPENAIP_CLIENT_H
#define OPENAIP_CLIENT_H

#include <mutex>
#include <string>
#include <utility>

/// HTTP client for the OpenAIP Tiles API (PNG TMS/XYZ).
/// Caches tiles under resources/openaip/cache/.
class OpenAipClient
{
public:
    static OpenAipClient &instance();

    /// Downloads one PNG tile if it is not already cached.
    /// @return Local cache path on success, empty string on failure.
    std::string fetchTile(int z, int x, int y, const std::string &layer = "openaip");

    /// Street, dark, or satellite base map (same XYZ grid as OpenAIP).
    std::string fetchBasemap(int z, int x, int y);

    /// voyager (Carto), dark (Carto Dark Matter), satellite (Esri imagery).
    void setBasemapStyle(const std::string &style);
    const std::string &basemapStyle() const { return mBasemapStyle; }
    const std::string &basemapLayer() const { return mBasemapLayer; }
    std::string cycleBasemapStyle();

    /// Fetches a square of tiles around a geographic position (no-op if already queued).
    void fetchAround(float latitude, float longitude, int zoom = 13, int radius = 7);

    std::string cachePath(int z, int x, int y, const std::string &layer = "openaip") const;

    bool isCached(int z, int x, int y, const std::string &layer = "openaip") const;

    static std::pair<int, int> latLonToTile(float latitude, float longitude, int zoom);
    static void latLonToPixels(float latitude, float longitude, int zoom, double &pixelX, double &pixelY);

private:
    OpenAipClient();
    OpenAipClient(const OpenAipClient &) = delete;
    OpenAipClient &operator=(const OpenAipClient &) = delete;

    std::string loadApiKey() const;
    bool downloadToFile(const std::string &url, const std::string &path, bool sendApiKey) const;

    std::string mApiKey;
    std::string mCartoKey;
    std::string mBasemapStyle = "voyager";
    std::string mBasemapLayer = "osm";
    std::string mCacheRoot;
    std::mutex mMutex;
    int mLastZ = -1;
    int mLastX = -1;
    int mLastY = -1;
};

#endif
