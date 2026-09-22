#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "ahrs_widget.h"
#include "asset_path.h"
#include "data_manager_sim.h"
#include "popup.h"
#include "render2d.h"
#include "screen.h"
#include "terrain_download.h"
#include "terrain_widget.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

enum class AhrsMode
{
    Only,
    Overlay,
    Off,
};

enum class MapMode
{
    Satellite,
    Simple,
};

enum class AipMode
{
    Walls,
    Overlay,
    Off,
};

class AppController : public IRenderer
{
    static constexpr int kMenuCols = 4;
    static constexpr int kMenuRows = 4;
    static constexpr uint64_t kMenuTimeoutMs = 3000;

    struct MenuItem
    {
        const char *label;
        int col;
        int row;
        bool header;
    };

    static constexpr MenuItem kItems[] = {
        {"AHRS", 0, 0, true},  {"ONLY", 0, 1, false}, {"OVRLY", 0, 2, false}, {"OFF", 0, 3, false},
        {"MAP", 1, 0, true},   {"SATT", 1, 1, false}, {"SMPL", 1, 2, false},
        {"AIP", 2, 0, true},   {"3D", 2, 1, false},   {"OVRLY", 2, 2, false}, {"OFF", 2, 3, false},
        {"CONF", 3, 0, true}, {"STATS", 3, 1, false}, {"RENDER", 3, 2, false},
    };
    static constexpr int kItemCount = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));

    class MenuOverlay final : public IWidget
    {
    public:
        MenuOverlay(Screen &screen, AppController &owner) : IWidget(screen), mOwner(owner) {}
        void render() override
        {
            mOwner.drawMenu();
            mOwner.drawPreload();
            mOwner.drawStats();
            mOwner.drawRenderPopup();
        }
        void setPos(int, int) override {}

    private:
        AppController &mOwner;
    };

public:
    AppController(Screen &screen, AhrsWidget &ahrs, TerrainWidget &terrain, DataManagerSim *sim)
        : mScreen(screen), mAhrs(ahrs), mTerrain(terrain), mSim(sim), mRenderPopup(screen)
    {
        screen.registerController(this);
        mBoxes.reserve(static_cast<size_t>(kItemCount));
        mLabels.reserve(static_cast<size_t>(kItemCount));
        for (int i = 0; i < kItemCount; ++i)
        {
            mBoxes.emplace_back(std::make_unique<Render2D>(screen));
            mLabels.emplace_back(std::make_unique<Render2D>(screen));
        }
        mOverlay = std::make_unique<MenuOverlay>(screen, *this);
        mPreloadBox = std::make_unique<Render2D>(screen);
        for (int i = 0; i < 4; ++i)
        {
            mPreloadLines.emplace_back(std::make_unique<Render2D>(screen));
        }
        mStatsBox = std::make_unique<Render2D>(screen);
        for (int i = 0; i < 10; ++i)
        {
            mStatsLines.emplace_back(std::make_unique<Render2D>(screen));
        }
        applyLayers();
        TerrainDownload::instance().prepare();
        mTerrain.setSatFarZoom(mFarZoom);
        mTerrain.setSatDetailZoom(mSatZoom);
        std::cout << "Keys: 1 AHRS only, 2 AHRS off, 3 overlay, 4 sat/simple, 5 AIP, Esc quit\n";
        std::cout << "Touch: tap anywhere for layer menu, hides after 3s idle\n";
        if (mSim)
        {
            std::cout << "Sim: arrows pitch/roll, Q/E heading, W/S speed, +/- alt, R reset\n";
        }
    }

    void render() override
    {
        expireMenu();
        tickFps();
        mTerrain.pumpMapPreload();
        if (!mSim)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        float dt = 0.016f;
        if (mHasClock)
        {
            dt = std::chrono::duration<float>(now - mLastTick).count();
        }
        mLastTick = now;
        mHasClock = true;

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        mSim->tick(dt,
                   keys[SDL_SCANCODE_UP],
                   keys[SDL_SCANCODE_DOWN],
                   keys[SDL_SCANCODE_LEFT],
                   keys[SDL_SCANCODE_RIGHT],
                   keys[SDL_SCANCODE_Q],
                   keys[SDL_SCANCODE_E],
                   keys[SDL_SCANCODE_W],
                   keys[SDL_SCANCODE_S],
                   keys[SDL_SCANCODE_PAGEUP] || keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS],
                   keys[SDL_SCANCODE_PAGEDOWN] || keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]);
    }

    bool keyDown(SDL_Keycode key) override
    {
        switch (key)
        {
        case SDLK_TAB:
            cycleAhrs();
            return true;
        case SDLK_1:
        case SDLK_F1:
            setAhrs(AhrsMode::Only);
            return true;
        case SDLK_2:
        case SDLK_F2:
            setAhrs(AhrsMode::Off);
            return true;
        case SDLK_3:
        case SDLK_F3:
            setAhrs(AhrsMode::Overlay);
            return true;
        case SDLK_4:
        case SDLK_F4:
            setMap(mMapMode == MapMode::Satellite ? MapMode::Simple : MapMode::Satellite);
            return true;
        case SDLK_5:
        case SDLK_F5:
            cycleAip();
            return true;
        case SDLK_r:
            if (mSim)
            {
                mSim->resetAttitude();
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    bool mouseClick(int x, int y) override
    {
        if (mRenderOpen)
        {
            const int control = mRenderPopup.hitButton(x, y);
            if (control >= 0)
            {
                adjustRender(control);
                return true;
            }
            if (mMenuVisible)
            {
                const int item = hitMenuItem(x, y);
                if (item >= 0 && kItems[item].col == 3 && kItems[item].row == 2)
                {
                    mRenderOpen = false;
                    mRenderKey.clear();
                    bumpMenuTimeout();
                    refreshMenu();
                    return true;
                }
            }
            if (mRenderPopup.contains(x, y))
            {
                return true;
            }
            mRenderOpen = false;
            mRenderKey.clear();
            refreshMenu();
            return true;
        }
        if (mMenuVisible)
        {
            bumpMenuTimeout();
            const int item = hitMenuItem(x, y);
            if (item >= 0)
            {
                activateItem(item);
            }
            return true;
        }
        showMenu();
        return true;
    }

private:
    void cycleAhrs()
    {
        if (mAhrsMode == AhrsMode::Overlay)
        {
            setAhrs(AhrsMode::Only);
        }
        else if (mAhrsMode == AhrsMode::Only)
        {
            setAhrs(AhrsMode::Off);
        }
        else
        {
            setAhrs(AhrsMode::Overlay);
        }
    }

    void cycleAip()
    {
        if (mAipMode == AipMode::Walls)
        {
            setAip(AipMode::Overlay);
        }
        else if (mAipMode == AipMode::Overlay)
        {
            setAip(AipMode::Off);
        }
        else
        {
            setAip(AipMode::Walls);
        }
    }

    void setAhrs(AhrsMode mode)
    {
        if (mode != AhrsMode::Only)
        {
            mAhrsWithMap = mode;
        }
        mAhrsMode = mode;
        applyLayers();
        refreshMenu();
    }

    void setMap(MapMode mode)
    {
        mMapMode = mode;
        if (mAhrsMode == AhrsMode::Only)
        {
            mAhrsMode = mAhrsWithMap;
        }
        mStatsKey.clear();
        mPreloadKey.clear();
        applyLayers();
        refreshMenu();
    }

    void setAip(AipMode mode)
    {
        mAipMode = mode;
        applyLayers();
        refreshMenu();
    }

    void applyLayers()
    {
        const bool mapOn = (mAhrsMode != AhrsMode::Only);
        mTerrain.enable(mapOn);
        mTerrain.setSatelliteGround(mapOn && mMapMode == MapMode::Satellite);
        mTerrain.setChartOverlay(mapOn && mAipMode == AipMode::Overlay);
        mTerrain.setAirspacesEnabled(mapOn && mAipMode == AipMode::Walls);
        mAhrs.enable(mAhrsMode != AhrsMode::Off);
        mAhrs.setDrawSkyGround(mAhrsMode == AhrsMode::Only);
    }

    void showMenu()
    {
        mMenuVisible = true;
        bumpMenuTimeout();
        rebuildMenuSprites();
    }

    void bumpMenuTimeout()
    {
        mMenuHideAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMenuTimeoutMs);
    }

    void expireMenu()
    {
        if (mMenuVisible && std::chrono::steady_clock::now() >= mMenuHideAt)
        {
            mMenuVisible = false;
        }
    }

    void refreshMenu()
    {
        if (mMenuVisible)
        {
            rebuildMenuSprites();
        }
    }

    bool itemActive(int index) const
    {
        const MenuItem &item = kItems[index];
        if (item.header)
        {
            return false;
        }
        if (item.col == 0)
        {
            if (item.row == 1)
            {
                return mAhrsMode == AhrsMode::Only;
            }
            if (item.row == 2)
            {
                return mAhrsMode == AhrsMode::Overlay;
            }
            return mAhrsMode == AhrsMode::Off;
        }
        if (item.col == 1)
        {
            if (mAhrsMode == AhrsMode::Only)
            {
                return false;
            }
            if (item.row == 1)
            {
                return mMapMode == MapMode::Satellite;
            }
            return mMapMode == MapMode::Simple;
        }
        if (item.col == 2)
        {
            if (item.row == 1)
            {
                return mAipMode == AipMode::Walls;
            }
            if (item.row == 2)
            {
                return mAipMode == AipMode::Overlay;
            }
            return mAipMode == AipMode::Off;
        }
        if (item.col == 3)
        {
            if (item.row == 2)
            {
                return mRenderOpen;
            }
            return mShowStats;
        }
        return false;
    }

    void activateItem(int index)
    {
        const MenuItem &item = kItems[index];
        if (item.header && item.col != 1)
        {
            return;
        }
        if (item.col == 0)
        {
            if (item.row == 1)
            {
                setAhrs(AhrsMode::Only);
            }
            else if (item.row == 2)
            {
                setAhrs(AhrsMode::Overlay);
            }
            else
            {
                setAhrs(AhrsMode::Off);
            }
            return;
        }
        if (item.col == 1)
        {
            if (item.header)
            {
                setMap(mMapMode);
                return;
            }
            setMap(item.row == 1 ? MapMode::Satellite : MapMode::Simple);
            return;
        }
        if (item.col == 2)
        {
            if (item.row == 1)
            {
                setAip(AipMode::Walls);
            }
            else if (item.row == 2)
            {
                setAip(AipMode::Overlay);
            }
            else
            {
                setAip(AipMode::Off);
            }
            return;
        }
        if (item.col == 3 && item.row == 1)
        {
            mShowStats = !mShowStats;
            mStatsKey.clear();
            mPreloadKey.clear();
            refreshMenu();
            return;
        }
        if (item.col == 3 && item.row == 2)
        {
            mRenderOpen = true;
            mRenderKey.clear();
            refreshMenu();
        }
    }

    void layoutMenu()
    {
        const int w = std::max(1, mScreen.getWidth());
        const int h = std::max(1, mScreen.getHeight());
        mMenuW = w;
        mMenuH = h;
        mMenuPad = std::max(4, h / 160);
        mMenuGap = std::max(4, w / 220);
        mMenuTop = mMenuPad;
        mMenuBoxH = std::max(36, h / 18);
        mMenuBoxW = std::max(48, (w - 2 * mMenuPad - (kMenuCols - 1) * mMenuGap) / kMenuCols);
        mMenuFont = std::clamp(mMenuBoxH * 2 / 5, 12, 22);
    }

    void cellRect(int col, int row, int &x, int &glY) const
    {
        x = mMenuPad + col * (mMenuBoxW + mMenuGap);
        const int sdlY = mMenuTop + row * (mMenuBoxH + mMenuGap);
        glY = mMenuH - sdlY - mMenuBoxH;
    }

    void rebuildMenuSprites()
    {
        layoutMenu();
        for (int i = 0; i < kItemCount; ++i)
        {
            const MenuItem &item = kItems[i];
            int x = 0;
            int glY = 0;
            cellRect(item.col, item.row, x, glY);
            uint32_t fill = 0xFFFFFF40u;
            if (item.header)
            {
                fill = 0x00000088u;
            }
            else if (itemActive(i))
            {
                fill = 0x4DA3FFB0u;
            }
            mBoxes[static_cast<size_t>(i)]->drawRectangle(x, glY, mMenuBoxW, mMenuBoxH, fill);
            mLabels[static_cast<size_t>(i)]->drawTextCentered(item.label, static_cast<float>(mMenuFont),
                                                              static_cast<float>(x + mMenuBoxW / 2),
                                                              static_cast<float>(glY + mMenuBoxH / 2), 0xFFFFFFFFu);
        }
    }

    int hitMenuItem(int x, int y) const
    {
        for (int i = 0; i < kItemCount; ++i)
        {
            const MenuItem &item = kItems[i];
            if (item.header && item.col != 1)
            {
                continue;
            }
            const int left = mMenuPad + item.col * (mMenuBoxW + mMenuGap);
            const int top = mMenuTop + item.row * (mMenuBoxH + mMenuGap);
            if (x >= left && x < left + mMenuBoxW && y >= top && y < top + mMenuBoxH)
            {
                return i;
            }
        }
        return -1;
    }

    static size_t readMemKb(const char *key)
    {
        FILE *file = std::fopen("/proc/meminfo", "r");
        if (file == nullptr)
        {
            return 0;
        }
        char name[64];
        unsigned long value = 0;
        char unit[32];
        size_t found = 0;
        while (std::fscanf(file, "%63s %lu %31s", name, &value, unit) == 3)
        {
            if (std::strcmp(name, key) == 0)
            {
                found = static_cast<size_t>(value);
                break;
            }
        }
        std::fclose(file);
        return found;
    }

    static int toMb(size_t bytes) { return static_cast<int>((bytes + 512u * 1024u) / (1024u * 1024u)); }

    void tickFps()
    {
        ++mFpsFrames;
        const uint64_t now = SDL_GetTicks64();
        if (mFpsStartMs == 0)
        {
            mFpsStartMs = now;
            return;
        }
        if (now - mFpsStartMs >= 1000)
        {
            mFps = mFpsFrames;
            mFpsFrames = 0;
            mFpsStartMs = now;
        }
    }

    void formatMapLoadLines(char *l0, size_t l0n, char *l1, size_t l1n, char *l2, size_t l2n, char *l3, size_t l3n)
    {
        const auto nearProg = mTerrain.nearPreload();
        const auto farProg = mTerrain.farPreload();
        const bool ready = mTerrain.mapPreloadReady();
        const int nearMb = toMb(nearProg.cpuBytes + nearProg.gpuBytes);
        const int farMb = toMb(farProg.cpuBytes + farProg.gpuBytes);
        const int totalSlots = std::max(1, nearProg.total + farProg.total);
        const int pct = (nearProg.done + farProg.done) * 100 / totalSlots;
        const size_t ramTotalKb = readMemKb("MemTotal:");
        const size_t ramAvailKb = readMemKb("MemAvailable:");
        if (ready)
        {
            std::snprintf(l0, l0n, "MAP READY");
        }
        else
        {
            std::snprintf(l0, l0n, "PRELOAD  %d%%", pct);
        }
        std::snprintf(l1, l1n, "NEAR Z%d  %d/%d  BUFFER %d MB", mSatZoom, nearProg.done, nearProg.total, nearMb);
        std::snprintf(l2, l2n, "FAR Z%d  %d/%d  BUFFER %d MB", mFarZoom, farProg.done, farProg.total, farMb);
        std::snprintf(l3, l3n, "RAM  %.1f / %.1f GB free", static_cast<double>(ramAvailKb) / (1024.0 * 1024.0),
                      static_cast<double>(ramTotalKb) / (1024.0 * 1024.0));
    }

    static size_t readFirstNumberFile(const char *path)
    {
        FILE *file = std::fopen(path, "r");
        if (file == nullptr)
        {
            return 0;
        }
        unsigned long value = 0;
        const int n = std::fscanf(file, "%lu", &value);
        std::fclose(file);
        return n == 1 ? static_cast<size_t>(value) : 0;
    }

    static bool glHasExtension(const char *name)
    {
        GLint count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i)
        {
            const char *ext = reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (ext != nullptr && std::strcmp(ext, name) == 0)
            {
                return true;
            }
        }
        return false;
    }

    /// Returns GPU free and total bytes. On mobile Mali/Adreno, memory is unified with RAM.
    static void queryVramBudget(size_t &freeBytes, size_t &totalBytes)
    {
        freeBytes = 0;
        totalBytes = 0;
#ifndef GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX 0x9047
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
#define GL_TEXTURE_FREE_MEMORY_ATI 0x87FC
#endif
        if (glHasExtension("GL_NVX_gpu_memory_info"))
        {
            GLint dedicatedKb = 0;
            GLint availKb = 0;
            glGetIntegerv(GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX, &dedicatedKb);
            glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availKb);
            if (dedicatedKb > 0)
            {
                totalBytes = static_cast<size_t>(dedicatedKb) * 1024u;
            }
            if (availKb > 0)
            {
                freeBytes = static_cast<size_t>(availKb) * 1024u;
            }
            if (freeBytes > 0 || totalBytes > 0)
            {
                return;
            }
        }
        if (glHasExtension("GL_ATI_meminfo") || glHasExtension("GL_NV_meminfo"))
        {
            GLint info[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, info);
            if (info[0] > 0)
            {
                freeBytes = static_cast<size_t>(info[0]) * 1024u;
                return;
            }
        }
        const size_t kgsl = readFirstNumberFile("/sys/class/kgsl/kgsl-3d0/gpu_available_memory");
        if (kgsl > 0)
        {
            freeBytes = kgsl > 1024u * 1024u * 16u ? kgsl : kgsl * 1024u;
            return;
        }
        const size_t ramAvailKb = readMemKb("MemAvailable:");
        const size_t ramTotalKb = readMemKb("MemTotal:");
        freeBytes = ramAvailKb * 1024u;
        totalBytes = ramTotalKb * 1024u;
    }

    void formatVramLine(char *out, size_t n) const
    {
        const size_t used = mTerrain.mapGpuBytes() + Shader::textureCacheBytes();
        size_t freeBytes = 0;
        size_t totalBytes = 0;
        queryVramBudget(freeBytes, totalBytes);
        if (totalBytes == 0 && freeBytes > 0)
        {
            totalBytes = used + freeBytes;
        }
        const int usedMb = toMb(used);
        const int freeMb = toMb(freeBytes);
        const int totalMb = toMb(totalBytes);
        if (totalMb > 0)
        {
            std::snprintf(out, n, "VRAM %d / %d MB  %d free", usedMb, totalMb, freeMb);
        }
        else
        {
            std::snprintf(out, n, "VRAM %d MB  %d MB free", usedMb, freeMb);
        }
    }

    void formatDownloadLines(char *dl0, size_t n0, char *dl1, size_t n1, char *dl2, size_t n2) const
    {
        const auto dl = OpenAipClient::instance().downloadStats();
        if (dl.inFlight > 0)
        {
            std::snprintf(dl0, n0, "DL SATT  %d ok  %d fail  %d get", dl.sattOk, dl.sattFail, dl.inFlight);
        }
        else
        {
            std::snprintf(dl0, n0, "DL SATT  %d ok  %d fail", dl.sattOk, dl.sattFail);
        }
        if (dl.hasAipKey)
        {
            std::snprintf(dl1, n1, "DL AIP   KEY  %d ok  %d fail", dl.aipOk, dl.aipFail);
        }
        else
        {
            std::snprintf(dl1, n1, "DL AIP   NO KEY");
        }
        const auto map = TerrainDownload::instance().stats();
        const int left = map.queued + map.inFlight;
        if (left > 0)
        {
            std::snprintf(dl2, n2, "DL TERR  %d ok  %d fail  %d left", map.ok, map.fail, left);
        }
        else
        {
            std::snprintf(dl2, n2, "DL TERR  %d ok  %d fail", map.ok, map.fail);
        }
    }

    static size_t directoryBytes(const std::string &root)
    {
        size_t total = 0;
        std::vector<std::string> pending;
        pending.push_back(root);
        while (!pending.empty())
        {
            const std::string dir = pending.back();
            pending.pop_back();
            DIR *handle = opendir(dir.c_str());
            if (!handle)
            {
                continue;
            }
            while (dirent *ent = readdir(handle))
            {
                if (ent->d_name[0] == '.' &&
                    (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
                {
                    continue;
                }
                const std::string path = dir + "/" + ent->d_name;
                struct stat info {};
                if (stat(path.c_str(), &info) != 0)
                {
                    continue;
                }
                if (S_ISDIR(info.st_mode))
                {
                    pending.push_back(path);
                }
                else if (S_ISREG(info.st_mode))
                {
                    total += static_cast<size_t>(info.st_size);
                }
            }
            closedir(handle);
        }
        return total;
    }

    void refreshCacheTotals(uint64_t nowMs)
    {
        if (mCacheScanMs != 0 && nowMs - mCacheScanMs < 2000)
        {
            return;
        }
        bool expected = false;
        if (!mCacheScanBusy.compare_exchange_strong(expected, true))
        {
            return;
        }
        mCacheScanMs = nowMs;
        const std::string satt = AssetPath::resolve("resources/openaip/cache/satellite");
        const std::string aip = AssetPath::resolve("resources/openaip/cache/openaip");
        const std::string terr = AssetPath::resolve("resources/terrain");
        std::thread([this, satt, aip, terr] {
            const size_t sattBytes = directoryBytes(satt);
            const size_t aipBytes = directoryBytes(aip);
            const size_t terrBytes = directoryBytes(terr);
            {
                std::lock_guard<std::mutex> lock(mCacheMutex);
                mCacheSatt = sattBytes;
                mCacheAip = aipBytes;
                mCacheTerr = terrBytes;
            }
            mCacheScanBusy.store(false);
        }).detach();
    }

    void formatCacheLine(char *out, size_t n, uint64_t nowMs)
    {
        refreshCacheTotals(nowMs);
        size_t sattBytes = 0;
        size_t aipBytes = 0;
        size_t terrBytes = 0;
        {
            std::lock_guard<std::mutex> lock(mCacheMutex);
            sattBytes = mCacheSatt;
            aipBytes = mCacheAip;
            terrBytes = mCacheTerr;
        }
        const int satt = toMb(sattBytes);
        const int aip = toMb(aipBytes);
        const int terr = toMb(terrBytes);
        std::snprintf(out, n, "CACHE  SATT %d MB  AIP %d MB  TERR %d MB", satt, aip, terr);
    }

    void drawStats()
    {
        if (!mShowStats)
        {
            return;
        }
        char fpsLine[80];
        char l0[96];
        char l1[96];
        char l2[96];
        char l3[96];
        char vram[96];
        char dl0[96];
        char dl1[96];
        char dl2[96];
        char cache[96];
        std::snprintf(fpsLine, sizeof(fpsLine), "FPS  %d  T %d/%d terrain drawn/loaded", mFps,
                      mTerrain.terrainDrawn(), mTerrain.terrainLoaded());
        formatMapLoadLines(l0, sizeof(l0), l1, sizeof(l1), l2, sizeof(l2), l3, sizeof(l3));
        formatVramLine(vram, sizeof(vram));
        formatDownloadLines(dl0, sizeof(dl0), dl1, sizeof(dl1), dl2, sizeof(dl2));
        const uint64_t nowMs = SDL_GetTicks64();
        formatCacheLine(cache, sizeof(cache), nowMs);
        const std::string key = std::string(fpsLine) + l0 + l1 + l2 + l3 + vram + dl0 + dl1 + dl2 + cache;
        const bool sizeChanged = mScreen.getWidth() != mStatsW || mScreen.getHeight() != mStatsH;
        const bool due = mStatsKey.empty() || sizeChanged || mStatsShownFps != mFps ||
                         (key != mStatsKey && nowMs - mStatsLastRebuildMs >= 250);
        if (due)
        {
            mStatsKey = key;
            mStatsShownFps = mFps;
            mStatsLastRebuildMs = nowMs;
            mStatsW = mScreen.getWidth();
            mStatsH = mScreen.getHeight();
            const int margin = std::max(10, mStatsH / 80);
            const int font = std::clamp(mStatsH / 48, 14, 20);
            const char *lines[10] = {fpsLine, l0, l1, l2, l3, vram, dl0, dl1, dl2, cache};
            static const char *kKeys[10] = {"efis-stat-0", "efis-stat-1", "efis-stat-2", "efis-stat-3",
                                            "efis-stat-4", "efis-stat-5", "efis-stat-6", "efis-stat-7",
                                            "efis-stat-8", "efis-stat-9"};
            int maxW = 0;
            int textH = font + 6;
            TTF_Init();
            TTF_Font *face = TTF_OpenFont(AssetPath::resolve("resources/fonts/B612Mono-Regular.ttf").c_str(), font);
            if (face)
            {
                for (int i = 0; i < 10; ++i)
                {
                    int w = 0;
                    int h = 0;
                    TTF_SizeText(face, lines[i], &w, &h);
                    maxW = std::max(maxW, w);
                    textH = std::max(textH, h);
                }
                TTF_CloseFont(face);
            }
            const int padX = std::max(12, font);
            const int padY = std::max(8, font / 2);
            const int lineH = textH + 4;
            const int boxW = maxW + 2 * padX;
            const int boxH = 10 * lineH + 2 * padY;
            const int x = mStatsW - margin - boxW;
            const int glY = margin;
            mStatsBox->drawRectangle(x, glY, boxW, boxH, 0x00000099u);
            for (int i = 0; i < 10; ++i)
            {
                const int ty = glY + padY + (9 - i) * lineH;
                mStatsLines[static_cast<size_t>(i)]->drawText(lines[i], static_cast<float>(font),
                                                              static_cast<float>(x + padX), static_cast<float>(ty),
                                                              0xFFFFFFFFu, kKeys[i]);
            }
        }
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        const glm::mat4 identity(1.0f);
        mStatsBox->setTransformationMatrix(identity);
        mStatsBox->render();
        for (auto &line : mStatsLines)
        {
            line->setTransformationMatrix(identity);
            line->render();
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    void rebuildPreloadSprites(const std::string &l0, const std::string &l1, const std::string &l2, const std::string &l3)
    {
        const int w = std::max(1, mScreen.getWidth());
        const int h = std::max(1, mScreen.getHeight());
        const int pad = std::max(8, h / 80);
        const int boxW = std::min(w - 2 * pad, std::max(460, w / 3));
        const int lineH = std::max(18, h / 36);
        const int boxH = lineH * 4 + pad;
        const int font = std::clamp(lineH - 4, 12, 20);
        const int x = pad;
        const int glY = pad;
        mPreloadBox->drawRectangle(x, glY, boxW, boxH, 0x00000099u);
        const char *lines[4] = {l0.c_str(), l1.c_str(), l2.c_str(), l3.c_str()};
        static const char *kKeys[4] = {"efis-pre-0", "efis-pre-1", "efis-pre-2", "efis-pre-3"};
        for (int i = 0; i < 4; ++i)
        {
            const int ty = glY + boxH - pad / 2 - lineH / 2 - i * lineH;
            mPreloadLines[static_cast<size_t>(i)]->drawText(lines[i], static_cast<float>(font),
                                                            static_cast<float>(x + pad), static_cast<float>(ty),
                                                            0xFFFFFFFFu, kKeys[i]);
        }
    }

    void drawPreload()
    {
        if (mShowStats)
        {
            return;
        }
        const bool ready = mTerrain.mapPreloadReady();
        if (ready)
        {
            if (!mPreloadWasReady)
            {
                mPreloadWasReady = true;
                mPreloadHideAt = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            }
            else if (std::chrono::steady_clock::now() >= mPreloadHideAt)
            {
                return;
            }
        }
        else
        {
            mPreloadWasReady = false;
        }

        char l0[96];
        char l1[96];
        char l2[96];
        char l3[96];
        formatMapLoadLines(l0, sizeof(l0), l1, sizeof(l1), l2, sizeof(l2), l3, sizeof(l3));
        const std::string key = std::string(l0) + l1 + l2 + l3;
        if (key != mPreloadKey)
        {
            mPreloadKey = key;
            rebuildPreloadSprites(l0, l1, l2, l3);
        }
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        const glm::mat4 identity(1.0f);
        mPreloadBox->setTransformationMatrix(identity);
        mPreloadBox->render();
        for (auto &line : mPreloadLines)
        {
            line->setTransformationMatrix(identity);
            line->render();
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    static int stepGrid(int grid, int delta)
    {
        static const int kGrids[] = {4, 8, 12, 16};
        int index = 1;
        for (int i = 0; i < 4; ++i)
        {
            if (kGrids[i] == grid)
            {
                index = i;
            }
        }
        index = std::clamp(index + delta, 0, 3);
        return kGrids[index];
    }

    static void formatKm(char *out, size_t n, double km)
    {
        if (km >= 10.0)
        {
            std::snprintf(out, n, "%.0f km", km);
        }
        else
        {
            std::snprintf(out, n, "%.1f km", km);
        }
    }

    double ringRadiusKm(int zoom, int grid) const
    {
        double lat = mTerrain.cameraLatitude();
        if (lat == 0.0)
        {
            lat = 50.959167;
        }
        const double cosLat = std::cos(lat * 3.14159265358979323846 / 180.0);
        const double tileM = 2.0 * 3.14159265358979323846 * 6378137.0 * cosLat / std::exp2(static_cast<double>(zoom));
        return (static_cast<double>(grid) * 0.5) * tileM / 1000.0;
    }

    void adjustRender(int control)
    {
        if (control == 0)
        {
            mFarGrid = stepGrid(mFarGrid, -1);
            mTerrain.setSatFarGrid(mFarGrid);
        }
        else if (control == 1)
        {
            mFarGrid = stepGrid(mFarGrid, 1);
            mTerrain.setSatFarGrid(mFarGrid);
        }
        else if (control == 2)
        {
            mFarZoom = std::clamp(mFarZoom - 1, 9, 12);
            mTerrain.setSatFarZoom(mFarZoom);
        }
        else if (control == 3)
        {
            mFarZoom = std::clamp(mFarZoom + 1, 9, 12);
            mTerrain.setSatFarZoom(mFarZoom);
        }
        else if (control == 4)
        {
            mNearGrid = stepGrid(mNearGrid, -1);
            mTerrain.setSatNearGrid(mNearGrid);
        }
        else if (control == 5)
        {
            mNearGrid = stepGrid(mNearGrid, 1);
            mTerrain.setSatNearGrid(mNearGrid);
        }
        else if (control == 6)
        {
            mSatZoom = std::clamp(mSatZoom - 1, 12, 18);
            mTerrain.setSatDetailZoom(mSatZoom);
        }
        else
        {
            mSatZoom = std::clamp(mSatZoom + 1, 12, 18);
            mTerrain.setSatDetailZoom(mSatZoom);
        }
        mRenderKey.clear();
        mStatsKey.clear();
        mPreloadKey.clear();
    }

    void drawRenderPopup()
    {
        if (!mRenderOpen)
        {
            return;
        }
        const int screenW = std::max(1, mScreen.getWidth());
        const int screenH = std::max(1, mScreen.getHeight());
        mRenderPopup.layout(screenW, screenH);
        char farDist[16];
        char nearDist[16];
        formatKm(farDist, sizeof(farDist), ringRadiusKm(mFarZoom, mFarGrid));
        formatKm(nearDist, sizeof(nearDist), ringRadiusKm(mSatZoom, mNearGrid));
        char farZoom[8];
        char nearZoom[8];
        std::snprintf(farZoom, sizeof(farZoom), "Z%d", mFarZoom);
        std::snprintf(nearZoom, sizeof(nearZoom), "Z%d", mSatZoom);
        const std::string key = std::string(farDist) + farZoom + nearDist + nearZoom + std::to_string(mRenderPopup.width()) +
                                std::to_string(mRenderPopup.height());
        if (key != mRenderKey)
        {
            mRenderKey = key;
            const int boxX = mRenderPopup.sdlX();
            const int boxY = mRenderPopup.sdlY();
            const int boxW = mRenderPopup.width();
            const int boxH = mRenderPopup.height();
            const int rowH = boxH / 4;
            const int btn = std::clamp(rowH * 2 / 3, 56, 140);
            const float font = static_cast<float>(std::clamp(btn / 2, 22, 52));
            const auto glMid = [&](int row) {
                const int sdlTop = boxY + row * rowH + rowH / 2;
                return screenH - sdlTop;
            };
            mRenderPopup.setText(0, "RENDER", font, static_cast<float>(boxX + boxW / 2), static_cast<float>(glMid(0)),
                                 "efis-render-title");
            const char *names[2] = {"FAR", "NEAR"};
            const char *dists[2] = {farDist, nearDist};
            const char *zooms[2] = {farZoom, nearZoom};
            static const char *kNameKeys[2] = {"efis-render-far", "efis-render-near"};
            static const char *kDistKeys[2] = {"efis-render-fard", "efis-render-neard"};
            static const char *kZoomKeys[2] = {"efis-render-farz", "efis-render-nearz"};
            for (int row = 0; row < 2; ++row)
            {
                const int sdlRowTop = boxY + (row + 1) * rowH;
                const int btnY = sdlRowTop + (rowH - btn) / 2;
                const float textY = static_cast<float>(glMid(row + 1));
                const int labelX = boxX + boxW / 10;
                const int distCenter = boxX + boxW * 42 / 100;
                const int zoomCenter = boxX + boxW * 78 / 100;
                const int distGap = btn + btn / 2;
                const int zoomGap = btn;
                mRenderPopup.setText(1 + row * 3, names[row], font, static_cast<float>(labelX), textY, kNameKeys[row]);
                mRenderPopup.setText(2 + row * 3, dists[row], font, static_cast<float>(distCenter), textY, kDistKeys[row]);
                mRenderPopup.setText(3 + row * 3, zooms[row], font, static_cast<float>(zoomCenter), textY, kZoomKeys[row]);
                const int base = row * 4;
                mRenderPopup.setButton(base + 0, distCenter - distGap - btn, btnY, btn, btn, "-", font, "efis-pop-minus");
                mRenderPopup.setButton(base + 1, distCenter + distGap, btnY, btn, btn, "+", font, "efis-pop-plus");
                mRenderPopup.setButton(base + 2, zoomCenter - zoomGap - btn, btnY, btn, btn, "-", font, "efis-pop-minus");
                mRenderPopup.setButton(base + 3, zoomCenter + zoomGap, btnY, btn, btn, "+", font, "efis-pop-plus");
            }
        }
        mRenderPopup.render();
    }

    void drawMenu()
    {
        expireMenu();
        if (!mMenuVisible)
        {
            return;
        }
        if (mScreen.getWidth() != mMenuW || mScreen.getHeight() != mMenuH)
        {
            rebuildMenuSprites();
        }
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const glm::mat4 identity(1.0f);
        for (int i = 0; i < kItemCount; ++i)
        {
            mBoxes[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mBoxes[static_cast<size_t>(i)]->render();
            mLabels[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mLabels[static_cast<size_t>(i)]->render();
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    Screen &mScreen;
    AhrsWidget &mAhrs;
    TerrainWidget &mTerrain;
    DataManagerSim *mSim;
    AhrsMode mAhrsMode = AhrsMode::Overlay;
    AhrsMode mAhrsWithMap = AhrsMode::Overlay;
    MapMode mMapMode = MapMode::Simple;
    int mSatZoom = 16;
    int mFarZoom = 12;
    int mFarGrid = 8;
    int mNearGrid = 8;
    bool mRenderOpen = false;
    std::string mRenderKey;
    Popup<8, 8> mRenderPopup;
    AipMode mAipMode = AipMode::Off;
    bool mShowStats = false;
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
    std::chrono::steady_clock::time_point mLastTick{};
    bool mHasClock = false;
    bool mMenuVisible = false;
    std::chrono::steady_clock::time_point mMenuHideAt{};
    int mMenuW = 0;
    int mMenuH = 0;
    int mMenuPad = 4;
    int mMenuTop = 4;
    int mMenuGap = 4;
    int mMenuBoxW = 80;
    int mMenuBoxH = 40;
    int mMenuFont = 16;
    std::vector<std::unique_ptr<Render2D>> mBoxes;
    std::vector<std::unique_ptr<Render2D>> mLabels;
    std::unique_ptr<MenuOverlay> mOverlay;
    std::unique_ptr<Render2D> mPreloadBox;
    std::vector<std::unique_ptr<Render2D>> mPreloadLines;
    std::string mPreloadKey;
    bool mPreloadWasReady = false;
    std::chrono::steady_clock::time_point mPreloadHideAt{};
    std::unique_ptr<Render2D> mStatsBox;
    std::vector<std::unique_ptr<Render2D>> mStatsLines;
};

#endif
