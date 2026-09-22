#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include "ahrs_widget.h"
#include "data_manager_sim.h"
#include "render2d.h"
#include "screen.h"
#include "terrain_widget.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
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
        {"CONF", 3, 0, true}, {"STATS", 3, 1, false},
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
        }
        void setPos(int, int) override {}

    private:
        AppController &mOwner;
    };

public:
    AppController(Screen &screen, AhrsWidget &ahrs, TerrainWidget &terrain, DataManagerSim *sim)
        : mScreen(screen), mAhrs(ahrs), mTerrain(terrain), mSim(sim)
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
        mStatsLine = std::make_unique<Render2D>(screen);
        applyLayers();
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

    void drawStats()
    {
        if (!mShowStats)
        {
            return;
        }
        char line[64];
        std::snprintf(line, sizeof(line), "FPS  %d", mFps);
        if (mStatsKey != line || mScreen.getWidth() != mStatsW || mScreen.getHeight() != mStatsH)
        {
            mStatsKey = line;
            mStatsW = mScreen.getWidth();
            mStatsH = mScreen.getHeight();
            const int pad = std::max(8, mStatsH / 80);
            const int boxW = std::max(120, mStatsW / 8);
            const int boxH = std::max(32, mStatsH / 18);
            const int font = std::clamp(boxH / 2, 14, 24);
            const int x = mStatsW - pad - boxW;
            const int glY = pad;
            mStatsBox->drawRectangle(x, glY, boxW, boxH, 0x00000099u);
            mStatsLine->drawTextCentered(line, static_cast<float>(font),
                                         static_cast<float>(x + boxW / 2),
                                         static_cast<float>(glY + boxH / 2), 0xFFFFFFFFu);
        }
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        const glm::mat4 identity(1.0f);
        mStatsBox->setTransformationMatrix(identity);
        mStatsBox->render();
        mStatsLine->setTransformationMatrix(identity);
        mStatsLine->render();
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    void rebuildPreloadSprites(const std::string &l0, const std::string &l1, const std::string &l2, const std::string &l3)
    {
        const int w = std::max(1, mScreen.getWidth());
        const int h = std::max(1, mScreen.getHeight());
        const int pad = std::max(8, h / 80);
        const int boxW = std::min(w - 2 * pad, std::max(280, w / 3));
        const int lineH = std::max(18, h / 36);
        const int boxH = lineH * 4 + pad;
        const int font = std::clamp(lineH - 4, 12, 20);
        const int x = pad;
        const int glY = pad;
        mPreloadBox->drawRectangle(x, glY, boxW, boxH, 0x00000099u);
        const char *lines[4] = {l0.c_str(), l1.c_str(), l2.c_str(), l3.c_str()};
        for (int i = 0; i < 4; ++i)
        {
            const int ty = glY + boxH - pad / 2 - lineH / 2 - i * lineH;
            mPreloadLines[static_cast<size_t>(i)]->drawText(lines[i], static_cast<float>(font),
                                                            static_cast<float>(x + pad), static_cast<float>(ty),
                                                            0xFFFFFFFFu);
        }
    }

    void drawPreload()
    {
        const auto nearProg = mTerrain.nearPreload();
        const auto farProg = mTerrain.farPreload();
        const bool ready = nearProg.ready && farProg.ready;
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

        const int nearMb = toMb(OpenAipAtlas::cpuBytesFor(OpenAipAtlas::kDetailRadius) +
                                OpenAipAtlas::gpuBytesFor(OpenAipAtlas::kDetailRadius));
        const int farMb = toMb(OpenAipAtlas::cpuBytesFor(OpenAipAtlas::kWideRadius) +
                               OpenAipAtlas::gpuBytesFor(OpenAipAtlas::kWideRadius));
        const int totalSlots = std::max(1, nearProg.total + farProg.total);
        const int pct = (nearProg.done + farProg.done) * 100 / totalSlots;
        const size_t ramTotalKb = readMemKb("MemTotal:");
        const size_t ramAvailKb = readMemKb("MemAvailable:");
        char l0[96];
        char l1[96];
        char l2[96];
        char l3[96];
        if (ready)
        {
            std::snprintf(l0, sizeof(l0), "MAP READY  %d MB resident", nearMb + farMb);
        }
        else
        {
            std::snprintf(l0, sizeof(l0), "PRELOAD SATT  %d%%", pct);
        }
        std::snprintf(l1, sizeof(l1), "NEAR %d MB  %d/%d", nearMb, nearProg.done, nearProg.total);
        std::snprintf(l2, sizeof(l2), "FAR  %d MB  %d/%d", farMb, farProg.done, farProg.total);
        std::snprintf(l3, sizeof(l3), "RAM  %.1f / %.1f GB free",
                      static_cast<double>(ramAvailKb) / (1024.0 * 1024.0),
                      static_cast<double>(ramTotalKb) / (1024.0 * 1024.0));
        const std::string key = std::string(l0) + l1 + l2 + l3;
        if (key != mPreloadKey)
        {
            mPreloadKey = key;
            rebuildPreloadSprites(l0, l1, l2, l3);
        }
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
    AipMode mAipMode = AipMode::Walls;
    bool mShowStats = false;
    int mFps = 0;
    int mFpsFrames = 0;
    uint64_t mFpsStartMs = 0;
    int mStatsW = 0;
    int mStatsH = 0;
    std::string mStatsKey;
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
    std::unique_ptr<Render2D> mStatsLine;
};

#endif
