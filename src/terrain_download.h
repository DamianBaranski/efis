#ifndef TERRAIN_DOWNLOAD_H
#define TERRAIN_DOWNLOAD_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>

/// Fetches missing FlightGear WS2 .btg.gz tiles into resources/terrain.
class TerrainDownload
{
public:
    static TerrainDownload &instance();

    /// Cache the Java download hook. Call this from the SDL thread before any worker runs.
    void prepare();

    /// Queue one local terrain path. No-op if the file exists or was already tried.
    void request(const std::string &absolutePath);

    /// True once, after a successful download of this path. The caller should reload it.
    bool takeReady(const std::string &absolutePath);

    struct Stats
    {
        int ok = 0;
        int fail = 0;
        int inFlight = 0;
        int queued = 0;
    };
    Stats stats() const;

private:
    TerrainDownload();
    TerrainDownload(const TerrainDownload &) = delete;
    TerrainDownload &operator=(const TerrainDownload &) = delete;

    struct Job
    {
        std::string path;
        std::string rel;
    };

    void ensureWorker();
    void workerLoop();
    bool downloadJob(const Job &job);

    mutable std::mutex mMutex;
    std::condition_variable mCv;
    std::deque<Job> mQueue;
    std::unordered_set<std::string> mQueued;
    std::unordered_set<std::string> mFailed;
    std::unordered_set<std::string> mReady;
    std::atomic<bool> mWorkerStarted{false};
    std::atomic<int> mOk{0};
    std::atomic<int> mFail{0};
    std::atomic<int> mInFlight{0};
};

#endif
