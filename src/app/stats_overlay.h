/// \file stats_overlay.h
/// Preload banner and the diagnostics panel.

#ifndef STATS_OVERLAY_H
#define STATS_OVERLAY_H

#include "iwidget.h"
#include "iworld_read.h"
#include "render2d.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class AppController;

/// Draws the map preload banner, or the diagnostics panel when STATS is on.
class StatsOverlay : public IWidget
{
public:
    /// Allocates the banner and the panel. The panel stays hidden until STATS is on.
    /// \param frame Loop that draws this overlay above the menu.
    /// \param controller Zoom values and the STATS flag.
    /// \param world Preload progress, altitude, and GPU bytes.
    StatsOverlay(Frame &frame, AppController &controller, IWorldRead &world);

    /// Counts frames and draws either the banner or the panel.
    void render() override;

    /// Unused. The overlay anchors itself to the window edges.
    void setPos(int, int) override {}

private:
    static size_t readMemKb(const char *key);
    static int toMb(size_t bytes);
    static size_t readFirstNumberFile(const char *path);
    static bool glHasExtension(const char *name);
    static void queryVramBudget(size_t &freeBytes, size_t &totalBytes);
    static size_t directoryBytes(const std::string &root);

    void tickFps();
    void formatMapLoadLines(char *l0, size_t l0n, char *l1, size_t l1n, char *l2, size_t l2n, char *l3, size_t l3n);
    void formatVramLine(char *out, size_t n) const;
    void formatDownloadLines(char *dl0, size_t n0, char *dl1, size_t n1, char *dl2, size_t n2) const;
    void refreshCacheTotals(uint64_t nowMs);
    void formatCacheLine(char *out, size_t n, uint64_t nowMs);
    void drawStats();
    void rebuildPreloadSprites(const std::string &l0, const std::string &l1, const std::string &l2,
                               const std::string &l3);
    void drawPreload();

    AppController &mController;
    IWorldRead &mWorld;
    int mFps = 0;
    int mStatsShownFps = -1;
    int mFpsFrames = 0;
    uint64_t mFpsStartMs = 0;
    int mStatsW = 0;
    int mStatsH = 0;
    std::string mStatsKey;
    uint64_t mStatsLastRebuildMs = 0;
    uint64_t mCacheScanMs = 0;
    std::atomic<bool> mCacheScanBusy{false};
    mutable std::mutex mCacheMutex;
    size_t mCacheSatt = 0;
    size_t mCacheAip = 0;
    size_t mCacheTerr = 0;
    std::unique_ptr<Render2D> mPreloadBox;
    std::vector<std::unique_ptr<Render2D>> mPreloadLines;
    std::string mPreloadKey;
    bool mPreloadWasReady = false;
    std::chrono::steady_clock::time_point mPreloadHideAt{};
    std::unique_ptr<Render2D> mStatsBox;
    std::vector<std::unique_ptr<Render2D>> mStatsLines;
};

#endif
