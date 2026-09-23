/// \file stats_overlay.cpp
/// Reads preload, memory, and download counters and draws them as HUD text.
#include "stats_overlay.h"
#include "app_controller.h"
#include "asset_path.h"
#include "openaip_client.h"
#include "shader.h"
#include "terrain_download.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <thread>
#include <glm/glm.hpp>

#ifndef GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX 0x9047
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
#define GL_TEXTURE_FREE_MEMORY_ATI 0x87FC
#endif

StatsOverlay::StatsOverlay(Frame &frame, AppController &controller, IWorldRead &world)
    : IWidget(frame), mController(controller), mWorld(world)
{
    mPreloadBox = std::make_unique<Render2D>(frame.screen());
    for (int i = 0; i < 4; ++i)
    {
        mPreloadLines.emplace_back(std::make_unique<Render2D>(frame.screen()));
    }
    mStatsBox = std::make_unique<Render2D>(frame.screen());
    for (int i = 0; i < 10; ++i)
    {
        mStatsLines.emplace_back(std::make_unique<Render2D>(frame.screen()));
    }
}

size_t StatsOverlay::readMemKb(const char *key)
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

int StatsOverlay::toMb(size_t bytes)
{
    return static_cast<int>((bytes + 512u * 1024u) / (1024u * 1024u));
}

size_t StatsOverlay::readFirstNumberFile(const char *path)
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

bool StatsOverlay::glHasExtension(const char *name)
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

void StatsOverlay::queryVramBudget(size_t &freeBytes, size_t &totalBytes)
{
    freeBytes = 0;
    totalBytes = 0;
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

size_t StatsOverlay::directoryBytes(const std::string &root)
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

void StatsOverlay::tickFps()
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

void StatsOverlay::formatMapLoadLines(char *l0, size_t l0n, char *l1, size_t l1n, char *l2, size_t l2n, char *l3,
                                      size_t l3n)
{
    const auto nearProg = mWorld.nearPreload();
    const auto farProg = mWorld.farPreload();
    const bool ready = mWorld.mapPreloadReady();
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
    std::snprintf(l1, l1n, "NEAR Z%d  %d/%d  BUFFER %d MB", mController.satZoom(), nearProg.done, nearProg.total, nearMb);
    std::snprintf(l2, l2n, "FAR Z%d  %d/%d  BUFFER %d MB", mController.farZoom(), farProg.done, farProg.total, farMb);
    std::snprintf(l3, l3n, "RAM  %.1f / %.1f GB free", static_cast<double>(ramAvailKb) / (1024.0 * 1024.0),
                  static_cast<double>(ramTotalKb) / (1024.0 * 1024.0));
}

void StatsOverlay::formatVramLine(char *out, size_t n) const
{
    const size_t used = mWorld.mapGpuBytes() + Shader::textureCacheBytes();
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

void StatsOverlay::formatDownloadLines(char *dl0, size_t n0, char *dl1, size_t n1, char *dl2, size_t n2) const
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

void StatsOverlay::refreshCacheTotals(uint64_t nowMs)
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

void StatsOverlay::formatCacheLine(char *out, size_t n, uint64_t nowMs)
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

void StatsOverlay::drawStats()
{
    char fpsLine[96];
    char l0[96];
    char l1[96];
    char l2[96];
    char l3[96];
    char vram[96];
    char dl0[96];
    char dl1[96];
    char dl2[96];
    char cache[96];
    const int altM = static_cast<int>(std::lround(mWorld.cameraAltitude()));
    const int altFt = static_cast<int>(std::lround(mWorld.cameraAltitude() * 3.280839895f));
    std::snprintf(fpsLine, sizeof(fpsLine), "FPS  %d  ALT %d m  %d ft  T %d/%d terrain drawn/loaded", mFps, altM, altFt,
                  mWorld.terrainDrawn(), mWorld.terrainLoaded());
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
        static const char *kKeys[10] = {"efis-stat-0", "efis-stat-1", "efis-stat-2", "efis-stat-3", "efis-stat-4",
                                        "efis-stat-5", "efis-stat-6", "efis-stat-7", "efis-stat-8", "efis-stat-9"};
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

void StatsOverlay::rebuildPreloadSprites(const std::string &l0, const std::string &l1, const std::string &l2,
                                         const std::string &l3)
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

void StatsOverlay::drawPreload()
{
    const bool ready = mWorld.mapPreloadReady();
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

void StatsOverlay::render()
{
    tickFps();
    if (mController.statsVisible())
    {
        drawStats();
        return;
    }
    drawPreload();
}
