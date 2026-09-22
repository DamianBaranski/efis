/// \file openaip_client.h
/// HTTP client for OpenAIP tiles and the Esri satellite base map.
#ifndef OPENAIP_CLIENT_H
#define OPENAIP_CLIENT_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>

/// HTTP client for the OpenAIP Tiles API (PNG TMS/XYZ).
/// Caches tiles under resources/openaip/cache/.
class OpenAipClient
{
public:
    /// Process-wide tile client.
    static OpenAipClient &instance();

    /// Downloads one PNG tile if it is not already cached.
    /// @return Local cache path on success, empty string on failure.
    std::string fetchTile(int z, int x, int y, const std::string &layer = "openaip");

    /// Esri World Imagery base map (same XYZ grid as OpenAIP).
    std::string fetchBasemap(int z, int x, int y);

    /// Cache folder name of the satellite layer.
    const std::string &basemapLayer() const { return mBasemapLayer; }

    /// Fetches a disk of tiles around a geographic position (no-op if already queued).
    void fetchAround(float latitude, float longitude, int zoom = 16, int radius = 7,
                     bool wantBasemap = true, bool wantOverlay = true);

    /// Queued jobs plus in-flight HTTP. Atlas uses this to keep retrying empty slots.
    int pendingDownloads() const;

    /// Absolute path of one cached PNG. The file may not exist yet.
    /// \param z XYZ zoom.
    /// \param x Tile column.
    /// \param y Tile row.
    /// \param layer Cache folder. `openaip` is the chart.
    std::string cachePath(int z, int x, int y, const std::string &layer = "openaip") const;

    /// True when that PNG is already on disk.
    bool isCached(int z, int x, int y, const std::string &layer = "openaip") const;

    /// True when resources hold an OpenAIP API key.
    bool hasApiKey() const { return !mApiKey.empty(); }

    /// Download counts since process start.
    struct DownloadStats
    {
        int sattOk = 0;      ///< Satellite tiles stored.
        int sattFail = 0;    ///< Satellite requests that failed.
        int aipOk = 0;       ///< Chart tiles stored.
        int aipFail = 0;     ///< Chart requests that failed.
        int inFlight = 0;    ///< HTTP requests running now.
        bool hasAipKey = false;
    };
    /// Snapshot of downloadStats fields.
    DownloadStats downloadStats() const;

    /// XYZ tile that contains the point at zoom.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    static std::pair<int, int> latLonToTile(float latitude, float longitude, int zoom);
    /// Pixel position inside the world map at zoom. Y grows south.
    static void latLonToPixels(float latitude, float longitude, int zoom, double &pixelX, double &pixelY);

private:
    OpenAipClient();
    OpenAipClient(const OpenAipClient &) = delete;
    OpenAipClient &operator=(const OpenAipClient &) = delete;

    std::string loadApiKey() const;
    bool downloadToFile(const std::string &url, const std::string &path, bool sendApiKey) const;
    void ensureWorker();
    void enqueueMissing(int zoom, int x, int y, bool aip);
    void workerLoop();
    static uint64_t jobId(int z, int x, int y, bool aip);

    struct Job
    {
        int z = 0;
        int x = 0;
        int y = 0;
        bool aip = false;
    };

    struct LastAround
    {
        int x = -1;
        int y = -1;
        int radius = -1;
        bool base = false;
        bool overlay = false;
    };

    std::string mApiKey;
    std::string mBasemapLayer = "satellite";
    std::string mCacheRoot;
    mutable std::mutex mMutex;
    std::condition_variable mCv;
    std::deque<Job> mQueue;
    std::unordered_set<uint64_t> mQueued;
    std::unordered_map<int, LastAround> mLastAround;
    std::atomic<bool> mWorkerStarted{false};
    std::atomic<int> mSattOk{0};
    std::atomic<int> mSattFail{0};
    std::atomic<int> mAipOk{0};
    std::atomic<int> mAipFail{0};
    std::atomic<int> mInFlight{0};
};

#endif
