/// \file general_settings.cpp
/// Loads and stores GENERAL in settings.json, and opens the platform list on Android.
#include "general_settings.h"

#include "app_controller.h"
#include "iworld_read.h"
#include "nav_voice.h"
#include "screen.h"
#include "session.h"
#include "settings_popup.h"

#include <SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef __ANDROID__
#include <jni.h>
#endif

namespace
{
AppController *gController = nullptr;
ISession *gSession = nullptr;
IWorldRead *gWorld = nullptr;
SettingsPopup *gPopup = nullptr;
bool gCoverage = false;
int gCovX = 0;
int gCovY = 0;
int gCovW = 0;
int gCovH = 0;
bool gLoading = false;
std::mutex gQueueMutex;
std::vector<std::pair<std::string, std::string>> gQueue;

std::string settingsPath()
{
    char *pref = SDL_GetPrefPath("efis", "efis");
    if (pref == nullptr || pref[0] == '\0')
    {
        SDL_free(pref);
        return "settings.json";
    }
    const std::string path = std::string(pref) + "settings.json";
    SDL_free(pref);
    return path;
}

double cameraLat()
{
    double lat = gWorld != nullptr ? gWorld->cameraLatitude() : 0.0;
    if (lat == 0.0)
    {
        lat = 50.959167;
    }
    return lat;
}

double radiusKm(int zoom, int grid)
{
    const double cosLat = std::cos(cameraLat() * 3.14159265358979323846 / 180.0);
    const double tileM = 2.0 * 3.14159265358979323846 * 6378137.0 * cosLat / std::exp2(static_cast<double>(zoom));
    return (static_cast<double>(grid) * 0.5) * tileM / 1000.0;
}

int clampGrid(int grid)
{
    static const int kGrids[] = {4, 8, 12, 16};
    int best = 8;
    int bestDelta = 100;
    for (int candidate : kGrids)
    {
        const int delta = std::abs(candidate - grid);
        if (delta < bestDelta)
        {
            best = candidate;
            bestDelta = delta;
        }
    }
    return best;
}

bool stepRadius(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int dir)
{
    static const int kGrids[] = {4, 8, 12, 16};
    const double cur = radiusKm(zoom, grid);
    int bestZ = zoom;
    int bestG = grid;
    double best = dir > 0 ? 1.0e9 : -1.0;
    bool found = false;
    for (int z = zoomLo; z <= zoomHi; ++z)
    {
        for (int g : kGrids)
        {
            const double km = radiusKm(z, g);
            if (km > maxKm + 1.0)
            {
                continue;
            }
            if (dir > 0 && km > cur + 0.2 && km < best)
            {
                best = km;
                bestZ = z;
                bestG = g;
                found = true;
            }
            if (dir < 0 && km < cur - 0.2 && km > best)
            {
                best = km;
                bestZ = z;
                bestG = g;
                found = true;
            }
        }
    }
    if (!found)
    {
        return false;
    }
    zoom = bestZ;
    grid = bestG;
    return true;
}

void stepZoom(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int delta)
{
    const int z = std::clamp(zoom + delta, zoomLo, zoomHi);
    if (z == zoom)
    {
        return;
    }
    int g = grid;
    static const int kGrids[] = {4, 8, 12, 16};
    while (radiusKm(z, g) > maxKm + 1.0)
    {
        int index = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (kGrids[i] == g)
            {
                index = i;
            }
        }
        if (index == 0)
        {
            return;
        }
        g = kGrids[index - 1];
    }
    zoom = z;
    grid = g;
}

bool flag(const std::string &value)
{
    return value == "1" || value == "true";
}

struct RingStep
{
    int zoom = 0;
    int grid = 0;
    double km = 0.0;
};

bool sameKmLabel(double a, double b)
{
    const auto bucket = [](double km) {
        return km >= 10.0 ? std::llround(km) : std::llround(km * 10.0);
    };
    return bucket(a) == bucket(b);
}

std::vector<RingStep> ringSteps(int zoomLo, int zoomHi, double maxKm)
{
    static const int kGrids[] = {4, 8, 12, 16};
    std::vector<RingStep> steps;
    for (int zoom = zoomLo; zoom <= zoomHi; ++zoom)
    {
        for (int grid : kGrids)
        {
            const double km = radiusKm(zoom, grid);
            if (km > maxKm + 1.0)
            {
                continue;
            }
            if (!steps.empty() && sameKmLabel(steps.back().km, km))
            {
                if (zoom > steps.back().zoom)
                {
                    steps.back() = RingStep{zoom, grid, km};
                }
                continue;
            }
            steps.push_back(RingStep{zoom, grid, km});
        }
    }
    std::sort(steps.begin(), steps.end(), [](const RingStep &a, const RingStep &b) { return a.km < b.km; });
    std::vector<RingStep> ordered;
    for (const RingStep &step : steps)
    {
        if (!ordered.empty() && sameKmLabel(ordered.back().km, step.km))
        {
            if (step.zoom > ordered.back().zoom)
            {
                ordered.back() = step;
            }
            continue;
        }
        ordered.push_back(step);
    }
    return ordered;
}

int ringIndex(const std::vector<RingStep> &steps, int zoom, int grid)
{
    for (int i = 0; i < static_cast<int>(steps.size()); ++i)
    {
        if (steps[static_cast<size_t>(i)].zoom == zoom && steps[static_cast<size_t>(i)].grid == grid)
        {
            return i;
        }
    }
    const double km = radiusKm(zoom, grid);
    int best = 0;
    double bestDelta = 1.0e9;
    for (int i = 0; i < static_cast<int>(steps.size()); ++i)
    {
        const double delta = std::fabs(steps[static_cast<size_t>(i)].km - km);
        if (delta < bestDelta)
        {
            best = i;
            bestDelta = delta;
        }
    }
    return best;
}

void applyNow(const std::string &key, const std::string &value)
{
    if (gController == nullptr || gSession == nullptr)
    {
        return;
    }
    NavVoice &voice = NavVoice::instance();
    if (key == "narration")
    {
        voice.setNarration(flag(value));
    }
    else if (key == "airspace")
    {
        voice.setAirspace(flag(value));
    }
    else if (key == "reporting")
    {
        voice.setReporting(flag(value));
    }
    else if (key == "nearest")
    {
        voice.setNearest(flag(value));
    }
    else if (key == "obstacles")
    {
        voice.setObstacles(flag(value));
    }
    else if (key == "voice")
    {
        voice.preferVoiceId(value);
    }
    else if (key == "voiceIndex")
    {
        voice.selectVoice(std::atoi(value.c_str()));
    }
    else if (key == "horizon")
    {
        gSession->toggleHorizon();
    }
    else if (key == "preview")
    {
        voice.preview();
    }
    else if (key == "source")
    {
        if (value == "internal")
        {
            gSession->setSource(SituationSource::Internal);
        }
        else if (value == "stratux")
        {
            gSession->setSource(SituationSource::Stratux);
        }
        else
        {
            gSession->setSource(SituationSource::Sim);
        }
    }
    else if (key == "gps")
    {
        gSession->setSensor(0, flag(value));
    }
    else if (key == "gyro")
    {
        gSession->setSensor(1, flag(value));
    }
    else if (key == "accelerometer")
    {
        gSession->setSensor(2, flag(value));
    }
    else if (key == "compass")
    {
        gSession->setSensor(3, flag(value));
    }
    else if (key == "layout")
    {
        Screen::setLayoutVertical(value == "vertical" || value == "1");
    }
    else if (key == "farZoom")
    {
        gController->setFarCoverage(std::clamp(std::atoi(value.c_str()), 9, 12), gController->farGrid());
    }
    else if (key == "farGrid")
    {
        gController->setFarCoverage(gController->farZoom(), clampGrid(std::atoi(value.c_str())));
    }
    else if (key == "nearZoom")
    {
        gController->setNearCoverage(std::clamp(std::atoi(value.c_str()), 12, 18), gController->nearGrid());
    }
    else if (key == "nearGrid")
    {
        gController->setNearCoverage(gController->satZoom(), clampGrid(std::atoi(value.c_str())));
    }
    else if (key == "far" || key == "near")
    {
        const bool far = key == "far";
        int zoom = far ? gController->farZoom() : gController->satZoom();
        int grid = far ? gController->farGrid() : gController->nearGrid();
        const int lo = far ? 9 : 12;
        const int hi = far ? 12 : 18;
        const double maxKm = far ? 50.0 : 10.0;
        if (value == "dist-")
        {
            stepRadius(zoom, grid, lo, hi, maxKm, -1);
        }
        else if (value == "dist+")
        {
            stepRadius(zoom, grid, lo, hi, maxKm, 1);
        }
        else if (value == "zoom-")
        {
            stepZoom(zoom, grid, lo, hi, maxKm, -1);
        }
        else if (value == "zoom+")
        {
            stepZoom(zoom, grid, lo, hi, maxKm, 1);
        }
        if (far)
        {
            gController->setFarCoverage(zoom, grid);
        }
        else
        {
            gController->setNearCoverage(zoom, grid);
        }
    }
    else if (key == "farIndex" || key == "nearIndex")
    {
        const bool far = key == "farIndex";
        const std::vector<RingStep> steps = ringSteps(far ? 9 : 12, far ? 12 : 18, far ? 50.0 : 10.0);
        const int index = std::atoi(value.c_str());
        if (index >= 0 && index < static_cast<int>(steps.size()))
        {
            const RingStep &step = steps[static_cast<size_t>(index)];
            if (far)
            {
                gController->setFarCoverage(step.zoom, step.grid);
            }
            else
            {
                gController->setNearCoverage(step.zoom, step.grid);
            }
        }
    }
    else if (key == "coverage")
    {
        gCoverage = flag(value);
    }
    else if (key == "coverageRect")
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        std::sscanf(value.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h);
        gCovX = x;
        gCovY = y;
        gCovW = w;
        gCovH = h;
    }
}

nlohmann::json currentJson()
{
    nlohmann::json doc = nlohmann::json::object();
    if (gController == nullptr || gSession == nullptr)
    {
        return doc;
    }
    NavVoice &voice = NavVoice::instance();
    doc["narration"] = voice.narration();
    doc["airspaceVoice"] = voice.airspace();
    doc["reportingVoice"] = voice.reporting();
    doc["nearestVoice"] = voice.nearest();
    doc["obstacleVoice"] = voice.obstacles();
    doc["voiceId"] = voice.selectedVoiceId();
    const char *source = "sim";
    if (gSession->source() == SituationSource::Internal)
    {
        source = "internal";
    }
    else if (gSession->source() == SituationSource::Stratux)
    {
        source = "stratux";
    }
    doc["source"] = source;
    doc["gps"] = gSession->sensorOn(0);
    doc["gyro"] = gSession->sensorOn(1);
    doc["accelerometer"] = gSession->sensorOn(2);
    doc["compass"] = gSession->sensorOn(3);
    doc["layout"] = Screen::layoutVertical() ? "vertical" : "horizontal";
    doc["farZoom"] = gController->farZoom();
    doc["farGrid"] = gController->farGrid();
    doc["nearZoom"] = gController->satZoom();
    doc["nearGrid"] = gController->nearGrid();
    return doc;
}

void writeFile()
{
    if (gLoading || gController == nullptr)
    {
        return;
    }
    const std::string path = settingsPath();
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out)
    {
        std::fprintf(stderr, "settings: cannot write %s\n", tmp.c_str());
        return;
    }
    out << currentJson().dump(2) << '\n';
    out.close();
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        std::fprintf(stderr, "settings: cannot replace %s\n", path.c_str());
    }
}

void readFile()
{
    const std::string path = settingsPath();
    std::ifstream in(path);
    if (!in)
    {
        return;
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object())
    {
        std::fprintf(stderr, "settings: %s is not a JSON object\n", path.c_str());
        return;
    }
    auto text = [&](const char *key) {
        if (!doc.contains(key) || !doc[key].is_string())
        {
            return std::string();
        }
        return doc[key].get<std::string>();
    };
    auto boolean = [&](const char *key, bool fallback) {
        if (!doc.contains(key) || !doc[key].is_boolean())
        {
            return fallback;
        }
        return doc[key].get<bool>();
    };
    auto integer = [&](const char *key, int fallback) {
        if (!doc.contains(key) || !doc[key].is_number_integer())
        {
            return fallback;
        }
        return doc[key].get<int>();
    };
    applyNow("narration", boolean("narration", true) ? "1" : "0");
    applyNow("airspace", boolean("airspaceVoice", true) ? "1" : "0");
    applyNow("reporting", boolean("reportingVoice", true) ? "1" : "0");
    applyNow("nearest", boolean("nearestVoice", true) ? "1" : "0");
    applyNow("obstacles", boolean("obstacleVoice", true) ? "1" : "0");
    const std::string voiceId = text("voiceId");
    if (!voiceId.empty())
    {
        applyNow("voice", voiceId);
    }
    const std::string source = text("source");
    if (!source.empty())
    {
        applyNow("source", source);
    }
    applyNow("gps", boolean("gps", true) ? "1" : "0");
    applyNow("gyro", boolean("gyro", true) ? "1" : "0");
    applyNow("accelerometer", boolean("accelerometer", true) ? "1" : "0");
    applyNow("compass", boolean("compass", true) ? "1" : "0");
    const std::string layout = text("layout");
    if (!layout.empty())
    {
        applyNow("layout", layout);
    }
    if (gController != nullptr)
    {
        applyNow("farZoom", std::to_string(integer("farZoom", gController->farZoom())));
        applyNow("farGrid", std::to_string(integer("farGrid", gController->farGrid())));
        applyNow("nearZoom", std::to_string(integer("nearZoom", gController->satZoom())));
        applyNow("nearGrid", std::to_string(integer("nearGrid", gController->nearGrid())));
    }
}

#ifdef __ANDROID__
void formatKm(char *out, size_t n, double km)
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

std::string xyzLine(bool sample, float x, float y, float z, const char *unit)
{
    if (!sample)
    {
        return "N/A";
    }
    char buf[80];
    std::snprintf(buf, sizeof(buf), "X %+.2f  Y %+.2f  Z %+.2f %s", x, y, z, unit);
    return buf;
}

std::string snapshotText()
{
    if (gController == nullptr || gSession == nullptr)
    {
        return {};
    }
    const NavVoice &voice = NavVoice::instance();
    char farDist[16];
    char nearDist[16];
    formatKm(farDist, sizeof(farDist), radiusKm(gController->farZoom(), gController->farGrid()));
    formatKm(nearDist, sizeof(nearDist), radiusKm(gController->satZoom(), gController->nearGrid()));
    std::string text;
    auto line = [&](const std::string &key, const std::string &value) {
        text += key;
        text += '=';
        text += value;
        text += '\n';
    };
    line("far", std::string(farDist) + "|Z" + std::to_string(gController->farZoom()));
    line("near", std::string(nearDist) + "|Z" + std::to_string(gController->satZoom()));
    const std::vector<RingStep> farSteps = ringSteps(9, 12, 50.0);
    const std::vector<RingStep> nearSteps = ringSteps(12, 18, 10.0);
    line("farIndex", std::to_string(ringIndex(farSteps, gController->farZoom(), gController->farGrid())));
    line("farCount", std::to_string(farSteps.size()));
    for (size_t i = 0; i < farSteps.size(); ++i)
    {
        char dist[16];
        formatKm(dist, sizeof(dist), farSteps[i].km);
        line("far" + std::to_string(i), std::string(dist) + "  ·  zoom " + std::to_string(farSteps[i].zoom));
    }
    line("nearIndex", std::to_string(ringIndex(nearSteps, gController->satZoom(), gController->nearGrid())));
    line("nearCount", std::to_string(nearSteps.size()));
    for (size_t i = 0; i < nearSteps.size(); ++i)
    {
        char dist[16];
        formatKm(dist, sizeof(dist), nearSteps[i].km);
        line("near" + std::to_string(i), std::string(dist) + "  ·  zoom " + std::to_string(nearSteps[i].zoom));
    }
    line("narration", voice.narration() ? "1" : "0");
    line("airspace", voice.airspace() ? "1" : "0");
    line("reporting", voice.reporting() ? "1" : "0");
    line("nearest", voice.nearest() ? "1" : "0");
    line("obstacles", voice.obstacles() ? "1" : "0");
    const std::vector<std::string> labels = NavVoice::instance().voiceLabels();
    line("voice", std::to_string(NavVoice::instance().voiceIndex()));
    line("voiceCount", std::to_string(labels.size()));
    for (size_t i = 0; i < labels.size(); ++i)
    {
        line("voice" + std::to_string(i), labels[i]);
    }
    const char *source = "sim";
    if (gSession->source() == SituationSource::Internal)
    {
        source = "internal";
    }
    else if (gSession->source() == SituationSource::Stratux)
    {
        source = "stratux";
    }
    line("source", source);
    line("receiver", gSession->receiver() ? "1" : "0");
    line("layout", Screen::layoutVertical() ? "vertical" : "horizontal");
    line("gps", gSession->sensorOn(0) ? "1" : "0");
    line("gyro", gSession->sensorOn(1) ? "1" : "0");
    line("accelerometer", gSession->sensorOn(2) ? "1" : "0");
    line("compass", gSession->sensorOn(3) ? "1" : "0");
    line("horizon", gSession->horizonSet() ? "1" : "0");
    const bool attitude = gSession->source() == SituationSource::Internal && gSession->sensors().attitude;
    line("attitude", attitude ? "1" : "0");
    const SensorReport report = gSession->source() == SituationSource::Internal ? gSession->sensors() : SensorReport{};
    if (!report.gps)
    {
        line("gpsLine", "N/A");
    }
    else
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.5f%c  %.5f%c  %.0f m  %.1f m/s", std::fabs(report.lat),
                      report.lat >= 0.0f ? 'N' : 'S', std::fabs(report.lon), report.lon >= 0.0f ? 'E' : 'W', report.alt,
                      report.speed);
        line("gpsLine", buf);
    }
    line("gyroLine", xyzLine(report.gyro, report.gx, report.gy, report.gz, "rad/s"));
    line("accelLine", xyzLine(report.accel, report.ax, report.ay, report.az, "m/s2"));
    line("compassLine", xyzLine(report.compass, report.mx, report.my, report.mz, "uT"));
    return text;
}
#endif

} // namespace

void bindGeneralSettings(AppController &controller, ISession &session, IWorldRead &world, SettingsPopup &popup)
{
    gController = &controller;
    gSession = &session;
    gWorld = &world;
    gPopup = &popup;
    gLoading = true;
    readFile();
    gLoading = false;
    NavVoice::instance().voiceIndex();
    writeFile();
}

void openGeneralSettings()
{
    if (gPopup != nullptr)
    {
        gPopup->invalidate();
    }
    if (gController != nullptr)
    {
        gController->setGeneralOpen(true);
    }
}

void saveGeneralSettings()
{
    writeFile();
}

void generalSettingsTick()
{
    std::vector<std::pair<std::string, std::string>> batch;
    {
        std::lock_guard<std::mutex> lock(gQueueMutex);
        batch.swap(gQueue);
    }
    if (batch.empty())
    {
        return;
    }
    for (const auto &item : batch)
    {
        applyNow(item.first, item.second);
    }
    writeFile();
}

bool coverageMapOpen()
{
    return gCoverage;
}

void coverageMapRect(int &x, int &y, int &w, int &h)
{
    x = gCovX;
    y = gCovY;
    w = gCovW;
    h = gCovH;
}

void closeCoverageMap()
{
    gCoverage = false;
}

#ifdef __ANDROID__
extern "C" JNIEXPORT jstring JNICALL Java_com_efis_app_GeneralSettings_snapshot(JNIEnv *env, jclass)
{
    const std::string text = snapshotText();
    return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT void JNICALL Java_com_efis_app_GeneralSettings_apply(JNIEnv *env, jclass, jstring key, jstring value)
{
    if (key == nullptr || value == nullptr)
    {
        return;
    }
    const char *keyChars = env->GetStringUTFChars(key, nullptr);
    const char *valueChars = env->GetStringUTFChars(value, nullptr);
    if (keyChars != nullptr && valueChars != nullptr)
    {
        std::lock_guard<std::mutex> lock(gQueueMutex);
        gQueue.emplace_back(keyChars, valueChars);
    }
    if (keyChars != nullptr)
    {
        env->ReleaseStringUTFChars(key, keyChars);
    }
    if (valueChars != nullptr)
    {
        env->ReleaseStringUTFChars(value, valueChars);
    }
}
#endif
