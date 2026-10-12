/// \file settings_popup.cpp
/// Draws GENERAL as a page list, described rows, and softkeys for the cursor row.
#include "settings_popup.h"
#include "app_controller.h"
#include "asset_path.h"
#include "general_settings.h"
#include "nav_voice.h"
#include "screen.h"
#include "stats_overlay.h"
#include <GLES3/gl3.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
constexpr uint32_t kPlate = 0x131920F0u;
constexpr uint32_t kPlateDim = 0x101820C0u;
constexpr uint32_t kCursor = 0x152030FFu;
constexpr uint32_t kEdge = 0x273444FFu;
constexpr uint32_t kEdgeOn = 0x39FF6AFFu;
constexpr uint32_t kInk = 0xFFFFFFFFu;
constexpr uint32_t kMuted = 0xA89988FFu;
constexpr const char *kPageName[] = {"IMAGERY", "MAPS", "SOUND", "SITUATION", "DISPLAY", "DEBUG"};

int textWidth(const std::string &text, int font)
{
    if (text.empty())
    {
        return 0;
    }
    const int guess = static_cast<int>(text.size()) * font * 3 / 5;
    if (TTF_Init() != 0)
    {
        return guess;
    }
    TTF_Font *face = TTF_OpenFont(AssetPath::resolve("resources/fonts/B612Mono-Regular.ttf").c_str(), font);
    if (face == nullptr)
    {
        return guess;
    }
    int w = 0;
    int h = 0;
    TTF_SizeText(face, text.c_str(), &w, &h);
    TTF_CloseFont(face);
    return w > 0 ? w : guess;
}

std::string xyzLine(bool sample, float x, float y, float z, const char *unit)
{
    if (!sample)
    {
        return {};
    }
    char buf[80];
    std::snprintf(buf, sizeof(buf), "X %+.2f  Y %+.2f  Z %+.2f %s", x, y, z, unit);
    return buf;
}

std::string gpsLine(const SensorReport &report)
{
    if (!report.gps)
    {
        return {};
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.5f%c  %.5f%c  %.0f m  %.1f m/s", std::fabs(report.lat),
                  report.lat >= 0.0f ? 'N' : 'S', std::fabs(report.lon), report.lon >= 0.0f ? 'E' : 'W', report.alt,
                  report.speed);
    return buf;
}

const char *onOff(bool on)
{
    return on ? "On" : "Off";
}
}

SettingsPopup::SettingsPopup(Frame &frame, AppController &controller, IWorldRead &world, ISession &session)
    : IWidget(frame), mController(controller), mWorld(world), mSession(session), mWindow(frame.screen()),
      mEuropeMap(frame.screen()), mCoverageFill(frame.screen())
{
}

void SettingsPopup::invalidate()
{
    mKey.clear();
}

void SettingsPopup::bindDebug(StatsOverlay &stats)
{
    mStats = &stats;
}

bool SettingsPopup::handleBack()
{
    if (!mVoicePage)
    {
        return false;
    }
    mVoicePage = false;
    mVoiceDrag = 0;
    mKey.clear();
    return true;
}

bool SettingsPopup::scrollBy(int dy)
{
    if (!mVoicePage)
    {
        return false;
    }
    mVoiceDrag += dy;
    constexpr int kStep = 48;
    bool moved = false;
    while (mVoiceDrag >= kStep && mVoiceScroll > 0)
    {
        mVoiceDrag -= kStep;
        --mVoiceScroll;
        moved = true;
    }
    while (mVoiceDrag <= -kStep && mVoiceScroll < mVoiceMax)
    {
        mVoiceDrag += kStep;
        ++mVoiceScroll;
        moved = true;
    }
    if (moved)
    {
        mKey.clear();
    }
    return true;
}

bool SettingsPopup::hitChart(int x, int y)
{
    if (coverageMapOpen())
    {
        return mEuropeMap.hit(x, y);
    }
    if (mWindow.activeTab() != 1 || mVoicePage)
    {
        return false;
    }
    if (x < mChartX || y < mChartY || x >= mChartX + mChartW || y >= mChartY + mChartH)
    {
        return false;
    }
    return mEuropeMap.hit(x, y);
}

int SettingsPopup::stepGrid(int grid, int delta)
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

void SettingsPopup::formatKm(char *out, size_t n, double km)
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

double SettingsPopup::ringRadiusKm(int zoom, int grid) const
{
    double lat = mWorld.cameraLatitude();
    if (lat == 0.0)
    {
        lat = 50.959167;
    }
    const double cosLat = std::cos(lat * 3.14159265358979323846 / 180.0);
    const double tileM = 2.0 * 3.14159265358979323846 * 6378137.0 * cosLat / std::exp2(static_cast<double>(zoom));
    return (static_cast<double>(grid) * 0.5) * tileM / 1000.0;
}

bool SettingsPopup::nextCoverage(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int dir) const
{
    static const int kGrids[] = {4, 8, 12, 16};
    const double cur = ringRadiusKm(zoom, grid);
    int bestZ = zoom;
    int bestG = grid;
    double best = dir > 0 ? 1.0e9 : -1.0;
    bool found = false;
    for (int z = zoomLo; z <= zoomHi; ++z)
    {
        for (int g : kGrids)
        {
            const double km = ringRadiusKm(z, g);
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

void SettingsPopup::nudgeZoom(int &zoom, int &grid, int zoomLo, int zoomHi, double maxKm, int delta) const
{
    const int z = std::clamp(zoom + delta, zoomLo, zoomHi);
    if (z == zoom)
    {
        return;
    }
    int g = grid;
    while (ringRadiusKm(z, g) > maxKm + 1.0)
    {
        const int smaller = stepGrid(g, -1);
        if (smaller == g)
        {
            return;
        }
        g = smaller;
    }
    zoom = z;
    grid = g;
}

void SettingsPopup::adjustRender(int control)
{
    int farZoom = mController.farZoom();
    int farGrid = mController.farGrid();
    int nearZoom = mController.satZoom();
    int nearGrid = mController.nearGrid();
    if (control == 0)
    {
        nextCoverage(farZoom, farGrid, 9, 12, kFarMaxKm, -1);
    }
    else if (control == 1)
    {
        nextCoverage(farZoom, farGrid, 9, 12, kFarMaxKm, 1);
    }
    else if (control == 2)
    {
        nudgeZoom(farZoom, farGrid, 9, 12, kFarMaxKm, -1);
    }
    else if (control == 3)
    {
        nudgeZoom(farZoom, farGrid, 9, 12, kFarMaxKm, 1);
    }
    else if (control == 4)
    {
        nextCoverage(nearZoom, nearGrid, 12, 18, kNearMaxKm, -1);
    }
    else if (control == 5)
    {
        nextCoverage(nearZoom, nearGrid, 12, 18, kNearMaxKm, 1);
    }
    else if (control == 6)
    {
        nudgeZoom(nearZoom, nearGrid, 12, 18, kNearMaxKm, -1);
    }
    else
    {
        nudgeZoom(nearZoom, nearGrid, 12, 18, kNearMaxKm, 1);
    }
    if (control <= 3)
    {
        mController.setFarCoverage(farZoom, farGrid);
    }
    else
    {
        mController.setNearCoverage(nearZoom, nearGrid);
    }
    mKey.clear();
    saveGeneralSettings();
}

void SettingsPopup::addSoft(const char *label, bool marked, Act action, int arg)
{
    Soft key;
    key.label = label;
    key.marked = marked;
    key.action = action;
    key.arg = arg;
    mSoft.push_back(std::move(key));
}

void SettingsPopup::layoutBody()
{
    const int screenW = std::max(1, mScreen.getWidth());
    const int screenH = std::max(1, mScreen.getHeight());
    for (int i = 0; i < kPages; ++i)
    {
        mWindow.setTabLabel(i, kPageName[i]);
    }
    mWindow.layout(screenW, screenH);
    const int tab = std::clamp(mWindow.activeTab(), 0, kPages - 1);
    mTitle = mVoicePage ? "GENERAL  /  VOICE" : std::string("GENERAL  /  ") + kPageName[tab];

    mLines.clear();
    mSoft.clear();
    mChartW = 0;
    mChartH = 0;

    const int boxX = mWindow.contentX() + 12;
    const int boxY = mWindow.contentY() + 12;
    const int boxW = std::max(1, mWindow.contentW() - 24);
    const int boxH = std::max(1, mWindow.contentH() - 24);
    const int gap = 8;

    auto placeRows = [&](int count) {
        count = std::max(1, count);
        int rowH = (boxH - gap * (count - 1)) / count;
        rowH = std::clamp(rowH, 52, 120);
        const int used = count * rowH + (count - 1) * gap;
        int y = boxY + std::max(0, (boxH - used) / 2);
        for (int i = 0; i < count && i < static_cast<int>(mLines.size()); ++i)
        {
            Line &line = mLines[static_cast<size_t>(i)];
            line.x = boxX;
            line.y = y;
            line.w = boxW;
            line.h = rowH;
            y += rowH + gap;
        }
    };

    if (mVoicePage)
    {
        const std::vector<std::string> labels = NavVoice::instance().voiceLabels();
        const int count = std::max(1, static_cast<int>(labels.size()));
        const int fit = std::max(1, boxH / (64 + gap));
        const int window = std::min(kMaxRows, fit);
        mVoiceMax = std::max(0, count - window);
        mVoiceScroll = std::clamp(mVoiceScroll, 0, mVoiceMax);
        mVoiceCursor = std::clamp(mVoiceCursor, 0, count - 1);
        const int inUse = NavVoice::instance().voiceIndex();
        const int shown = std::min(window, count - mVoiceScroll);
        for (int i = 0; i < shown; ++i)
        {
            const int index = mVoiceScroll + i;
            Line line;
            line.id = index;
            line.name = index < static_cast<int>(labels.size()) ? labels[static_cast<size_t>(index)] : "DEFAULT";
            line.cursor = index == mVoiceCursor;
            line.marked = index == inUse;
            if (line.marked)
            {
                line.value = "In use";
            }
            mLines.push_back(std::move(line));
        }
        if (mLines.empty())
        {
            Line line;
            line.name = "DEFAULT";
            line.cursor = true;
            line.marked = true;
            line.value = "In use";
            mLines.push_back(std::move(line));
        }
        placeRows(static_cast<int>(mLines.size()));
        addSoft("SELECT", false, Act::UseVoice);
        return;
    }

    auto push = [&](int id, const std::string &name, const std::string &sentence, const std::string &value, bool dim,
                    bool marked) {
        Line line;
        line.id = id;
        line.name = name;
        line.sentence = sentence;
        line.value = value;
        line.dim = dim;
        line.cursor = id == mCursor[tab];
        line.marked = marked;
        mLines.push_back(std::move(line));
    };

    if (tab == 0)
    {
        char farDist[24];
        char nearDist[24];
        formatKm(farDist, sizeof(farDist), ringRadiusKm(mController.farZoom(), mController.farGrid()));
        formatKm(nearDist, sizeof(nearDist), ringRadiusKm(mController.satZoom(), mController.nearGrid()));
        char farValue[40];
        char nearValue[40];
        std::snprintf(farValue, sizeof(farValue), "%s  Z%d", farDist, mController.farZoom());
        std::snprintf(nearValue, sizeof(nearValue), "%s  Z%d", nearDist, mController.satZoom());
        push(0, "Far ring", "How far imagery is loaded", farValue, false, false);
        push(1, "Near ring", "The inner imagery radius", nearValue, false, false);
        mCursor[0] = std::clamp(mCursor[0], 0, 1);
        mLines[0].cursor = mCursor[0] == 0;
        mLines[1].cursor = mCursor[0] == 1;
        placeRows(2);
        const bool near = mCursor[0] == 1;
        addSoft("CLOSER", false, near ? Act::NearCloser : Act::FarCloser);
        addSoft("FARTHER", false, near ? Act::NearFarther : Act::FarFarther);
        addSoft("ZOOM OUT", false, near ? Act::NearZoomOut : Act::FarZoomOut);
        addSoft("ZOOM IN", false, near ? Act::NearZoomIn : Act::FarZoomIn);
    }
    else if (tab == 1)
    {
        const int cap = 36;
        mChartX = mWindow.contentX();
        mChartY = mWindow.contentY() + cap;
        mChartW = mWindow.contentW();
        mChartH = std::max(1, mWindow.contentH() - cap);
    }
    else if (tab == 2)
    {
        NavVoice &voice = NavVoice::instance();
        const bool spoken = voice.narration();
        push(0, "Narration", "Master switch for spoken calls", onOff(spoken), false, spoken);
        push(1, "Voice", "Which installed voice speaks", voice.voiceLabel(), false, false);
        push(2, "Airspace", "Spoken call. Drawing is the Walls switch.", onOff(voice.airspace()), !spoken,
             voice.airspace());
        push(3, "Points", "Spoken call. Drawing is the Points switch.", onOff(voice.reporting()), !spoken,
             voice.reporting());
        push(4, "Nearest", "Spoken call for the nearest field.", onOff(voice.nearest()), !spoken, voice.nearest());
        push(5, "Obstacles", "Spoken call. Drawing is the Obstacles switch.", onOff(voice.obstacles()), !spoken,
             voice.obstacles());
        push(6, "Test phrase", "Speaks a short preview.", "Sample", false, false);
        mCursor[2] = std::clamp(mCursor[2], 0, 6);
        for (Line &line : mLines)
        {
            line.cursor = line.id == mCursor[2];
        }
        placeRows(7);
        const int row = mCursor[2];
        if (row == 0)
        {
            addSoft("ON", spoken, Act::NarrOn);
            addSoft("OFF", !spoken, Act::NarrOff);
        }
        else if (row == 1)
        {
            const std::vector<std::string> labels = voice.voiceLabels();
            bool longName = labels.size() > 4;
            for (const std::string &label : labels)
            {
                if (label.size() > 16)
                {
                    longName = true;
                }
            }
            if (longName || labels.empty())
            {
                addSoft("VOICES", false, Act::OpenVoices);
            }
            else
            {
                const int current = voice.voiceIndex();
                for (int i = 0; i < static_cast<int>(labels.size()); ++i)
                {
                    addSoft(labels[static_cast<size_t>(i)].c_str(), i == current, Act::UseVoice, i);
                }
            }
        }
        else if (row == 6)
        {
            addSoft("PLAY", false, Act::Play);
        }
        else
        {
            const bool on = mLines[static_cast<size_t>(row)].marked;
            const Act onAct = row == 2 ? Act::AirOn : (row == 3 ? Act::PointOn : (row == 4 ? Act::NearOn : Act::ObstOn));
            const Act offAct =
                row == 2 ? Act::AirOff : (row == 3 ? Act::PointOff : (row == 4 ? Act::NearOff : Act::ObstOff));
            addSoft("ON", on, onAct);
            addSoft("OFF", !on, offAct);
        }
    }
    else if (tab == 3)
    {
        const char *sourceName = "Simulator";
        if (mSession.source() == SituationSource::Internal)
        {
            sourceName = "Tablet";
        }
        else if (mSession.source() == SituationSource::Stratux)
        {
            sourceName = "Stratux";
        }
        push(0, "Source", "Where attitude and position come from", sourceName, false, false);
        push(1, "GPS", "Position from the tablet", onOff(mSession.sensorOn(0)), false, mSession.sensorOn(0));
        push(2, "Gyro", "Rotation rate from the tablet", onOff(mSession.sensorOn(1)), false, mSession.sensorOn(1));
        push(3, "Accelerometer", "Specific force from the tablet", onOff(mSession.sensorOn(2)), false,
             mSession.sensorOn(2));
        push(4, "Compass", "Magnetic field from the tablet", onOff(mSession.sensorOn(3)), false, mSession.sensorOn(3));
        const bool level = mSession.horizonSet();
        const bool attitude = mSession.source() == SituationSource::Internal && mSession.sensors().attitude;
        const char *horizon = level ? "Horizon set" : (attitude ? "Ready" : "Wait");
        push(5, "Horizon", "Level the attitude picture", horizon, false, level);
        mCursor[3] = std::clamp(mCursor[3], 0, 5);
        for (Line &line : mLines)
        {
            line.cursor = line.id == mCursor[3];
        }
        placeRows(6);
        const int row = mCursor[3];
        if (row == 0)
        {
            const int selected = mSession.source() == SituationSource::Internal
                                     ? 1
                                     : (mSession.source() == SituationSource::Stratux ? 2 : 0);
            addSoft("SIMULATOR", selected == 0, Act::SourceSim);
            addSoft("TABLET", selected == 1, Act::SourceTab);
            if (mSession.receiver())
            {
                addSoft("STRATUX", selected == 2, Act::SourceRadio);
            }
        }
        else if (row >= 1 && row <= 4)
        {
            const bool on = mSession.sensorOn(row - 1);
            addSoft("ON", on, Act::SensOn, row - 1);
            addSoft("OFF", !on, Act::SensOff, row - 1);
        }
        else if (attitude && !level)
        {
            addSoft("SET", false, Act::Horizon);
        }
    }
    else if (tab == 4)
    {
        const bool vertical = Screen::layoutVertical();
        push(0, "Layout", vertical ? "Attitude above the map" : "Attitude beside the map",
             vertical ? "Vertical" : "Horizontal", false, false);
        mCursor[4] = 0;
        mLines[0].cursor = true;
        placeRows(1);
        addSoft("HORIZONTAL", !vertical, Act::LayHoriz);
        addSoft("VERTICAL", vertical, Act::LayVert);
    }
}

void SettingsPopup::drawBody(int screenH)
{
    const float titleFont = static_cast<float>(std::clamp(mWindow.titleHeight() / 4, 18, 28));
    const float titleY = static_cast<float>(screenH - mWindow.titleHeight() / 2) - titleFont * 0.35f;
    mWindow.setTextLeft(0, mTitle, titleFont, 104.0f, titleY, kInk, (std::string("efis-general-title-") + mTitle).c_str());

    if (!mVoicePage && mWindow.activeTab() == 1)
    {
        const float y = static_cast<float>(screenH - (mWindow.contentY() + 28));
        mWindow.setTextLeft(1, "Shaded countries have stored terrain tiles.", 16.0f,
                            static_cast<float>(mWindow.contentX() + 16), y, kMuted, "efis-maps-caption");
    }
    else if (!mVoicePage && mWindow.activeTab() == 5)
    {
        const float y = static_cast<float>(screenH - (mWindow.contentY() + 28));
        mWindow.setTextLeft(1, "Frame rate, terrain load, sensors, and cache.", 16.0f,
                            static_cast<float>(mWindow.contentX() + 16), y, kMuted, "efis-debug-caption");
    }

    const int limit = std::min(static_cast<int>(mLines.size()), TabWindow<kPages, 24, 6>::kPlates);
    for (int i = 0; i < limit; ++i)
    {
        const Line &line = mLines[static_cast<size_t>(i)];
        const uint32_t fill = line.cursor ? kCursor : (line.dim ? kPlateDim : kPlate);
        const uint32_t edge = line.marked ? kEdgeOn : kEdge;
        mWindow.setPlate(i, line.x, line.y, line.w, line.h, fill, edge);

        const int nameFont = std::clamp(line.h / 4, 15, 24);
        const int sentFont = std::clamp(line.h / 5, 13, 18);
        const int left = line.x + 18;
        const int right = line.x + line.w - 18;
        const int glBottom = screenH - (line.y + line.h);
        const int nameSlot = 2 + i * 3;
        const std::string nameKey = "efis-row-name-" + std::to_string(mWindow.activeTab()) + "-" +
                                    std::to_string(line.id) + "-" + line.name;
        if (line.sentence.empty())
        {
            const float y = static_cast<float>(glBottom + (line.h - nameFont) / 2);
            mWindow.setTextLeft(nameSlot, line.name, static_cast<float>(nameFont), static_cast<float>(left), y, kInk,
                                nameKey.c_str());
            if (!line.value.empty())
            {
                const int vw = textWidth(line.value, nameFont);
                const float vy = y;
                mWindow.setTextLeft(nameSlot + 2, line.value, static_cast<float>(nameFont),
                                    static_cast<float>(std::max(left, right - vw)), vy, kInk,
                                    (nameKey + "-v-" + line.value).c_str());
            }
        }
        else
        {
            const int nameW = textWidth(line.name, nameFont);
            const int valueW = textWidth(line.value, nameFont);
            const bool valueOnTop = !line.value.empty() && nameW + 28 + valueW < (right - left);
            const float nameY = static_cast<float>(glBottom + line.h * 58 / 100);
            const float sentY = static_cast<float>(glBottom + line.h * 18 / 100);
            mWindow.setTextLeft(nameSlot, line.name, static_cast<float>(nameFont), static_cast<float>(left), nameY, kInk,
                                nameKey.c_str());
            if (valueOnTop)
            {
                mWindow.setTextLeft(nameSlot + 2, line.value, static_cast<float>(nameFont),
                                    static_cast<float>(right - valueW), nameY, kInk, (nameKey + "-v-" + line.value).c_str());
                mWindow.setTextLeft(nameSlot + 1, line.sentence, static_cast<float>(sentFont), static_cast<float>(left),
                                    sentY, kMuted, (nameKey + "-s-" + line.sentence).c_str());
            }
            else if (!line.value.empty())
            {
                mWindow.setTextLeft(nameSlot + 1, line.value, static_cast<float>(sentFont), static_cast<float>(left), sentY,
                                    kInk, (nameKey + "-v2-" + line.value).c_str());
            }
            else
            {
                mWindow.setTextLeft(nameSlot + 1, line.sentence, static_cast<float>(sentFont), static_cast<float>(left),
                                    sentY, kMuted, (nameKey + "-s-" + line.sentence).c_str());
            }
        }
    }

    const int keyCount = std::min(static_cast<int>(mSoft.size()), 6);
    if (keyCount > 0)
    {
        const int gap = 10;
        const int barY = mWindow.softY() + 10;
        const int barH = std::max(40, mWindow.softH() - 20);
        const int keyW = std::max(1, (mWindow.width() - gap * (keyCount + 1)) / keyCount);
        const float font = static_cast<float>(std::clamp(barH / 3, 14, 22));
        for (int i = 0; i < keyCount; ++i)
        {
            Soft &key = mSoft[static_cast<size_t>(i)];
            key.x = gap + i * (keyW + gap);
            key.y = barY;
            key.w = keyW;
            key.h = barH;
            const std::string cache = std::string("efis-soft-") + key.label + (key.marked ? "-on" : "-off");
            mWindow.setButton(i, key.x, key.y, key.w, key.h, key.label, font, cache.c_str(), kPlate,
                              key.marked ? kEdgeOn : kEdge);
        }
    }
}

void SettingsPopup::refreshLive(int screenH)
{
    if (mVoicePage || mWindow.activeTab() != 3)
    {
        return;
    }
    const SensorReport report = mSession.source() == SituationSource::Internal ? mSession.sensors() : SensorReport{};
    const bool internal = mSession.source() == SituationSource::Internal;
    const std::string live[4] = {internal ? gpsLine(report) : std::string(),
                                 internal ? xyzLine(report.gyro, report.gx, report.gy, report.gz, "rad/s") : std::string(),
                                 internal ? xyzLine(report.accel, report.ax, report.ay, report.az, "m/s2") : std::string(),
                                 internal ? xyzLine(report.compass, report.mx, report.my, report.mz, "uT") : std::string()};
    for (int s = 0; s < 4; ++s)
    {
        const Line *line = nullptr;
        int slot = -1;
        for (int i = 0; i < static_cast<int>(mLines.size()) && i < TabWindow<kPages, 24, 6>::kPlates; ++i)
        {
            if (mLines[static_cast<size_t>(i)].id == s + 1)
            {
                line = &mLines[static_cast<size_t>(i)];
                slot = 2 + i * 3 + 1;
                break;
            }
        }
        if (line == nullptr)
        {
            continue;
        }
        const std::string shown = live[s].empty() ? line->sentence : live[s];
        if (shown == mSensorValue[s])
        {
            continue;
        }
        mSensorValue[s] = shown;
        const int sentFont = std::clamp(line->h / 5, 13, 18);
        const int glBottom = screenH - (line->y + line->h);
        const float sentY = static_cast<float>(glBottom + line->h * 18 / 100);
        const uint32_t color = live[s].empty() ? kMuted : kInk;
        const std::string key = "efis-sensor-live-" + std::to_string(s) + "-" + shown;
        mWindow.setTextLeft(slot, shown, static_cast<float>(sentFont), static_cast<float>(line->x + 18), sentY, color,
                            key.c_str());
    }
}

void SettingsPopup::apply(const Soft &key)
{
    NavVoice &voice = NavVoice::instance();
    switch (key.action)
    {
    case Act::FarCloser:
        adjustRender(0);
        return;
    case Act::FarFarther:
        adjustRender(1);
        return;
    case Act::FarZoomOut:
        adjustRender(2);
        return;
    case Act::FarZoomIn:
        adjustRender(3);
        return;
    case Act::NearCloser:
        adjustRender(4);
        return;
    case Act::NearFarther:
        adjustRender(5);
        return;
    case Act::NearZoomOut:
        adjustRender(6);
        return;
    case Act::NearZoomIn:
        adjustRender(7);
        return;
    case Act::NarrOn:
        if (!voice.narration())
        {
            voice.setNarration(true);
        }
        break;
    case Act::NarrOff:
        if (voice.narration())
        {
            voice.setNarration(false);
        }
        break;
    case Act::AirOn:
        if (!voice.airspace())
        {
            voice.setAirspace(true);
        }
        break;
    case Act::AirOff:
        if (voice.airspace())
        {
            voice.setAirspace(false);
        }
        break;
    case Act::PointOn:
        if (!voice.reporting())
        {
            voice.setReporting(true);
        }
        break;
    case Act::PointOff:
        if (voice.reporting())
        {
            voice.setReporting(false);
        }
        break;
    case Act::NearOn:
        if (!voice.nearest())
        {
            voice.setNearest(true);
        }
        break;
    case Act::NearOff:
        if (voice.nearest())
        {
            voice.setNearest(false);
        }
        break;
    case Act::ObstOn:
        if (!voice.obstacles())
        {
            voice.setObstacles(true);
        }
        break;
    case Act::ObstOff:
        if (voice.obstacles())
        {
            voice.setObstacles(false);
        }
        break;
    case Act::Play:
        voice.preview();
        return;
    case Act::OpenVoices:
        mVoicePage = true;
        mVoiceCursor = voice.voiceIndex();
        mVoiceScroll = std::max(0, mVoiceCursor - 3);
        mVoiceDrag = 0;
        mKey.clear();
        return;
    case Act::UseVoice:
        voice.selectVoice(mVoicePage ? mVoiceCursor : key.arg);
        mVoicePage = false;
        break;
    case Act::SourceSim:
        if (mSession.source() != SituationSource::Sim)
        {
            mSession.setSource(SituationSource::Sim);
        }
        break;
    case Act::SourceTab:
        if (mSession.source() != SituationSource::Internal)
        {
            mSession.setSource(SituationSource::Internal);
        }
        break;
    case Act::SourceRadio:
        if (!mSession.receiver() || mSession.source() == SituationSource::Stratux)
        {
            break;
        }
        mSession.setSource(SituationSource::Stratux);
        break;
    case Act::SensOn:
        if (!mSession.sensorOn(key.arg))
        {
            mSession.toggleSensor(key.arg);
        }
        break;
    case Act::SensOff:
        if (mSession.sensorOn(key.arg))
        {
            mSession.toggleSensor(key.arg);
        }
        break;
    case Act::Horizon:
        mSession.toggleHorizon();
        mKey.clear();
        return;
    case Act::LayHoriz:
        if (Screen::layoutVertical())
        {
            Screen::setLayoutVertical(false);
        }
        break;
    case Act::LayVert:
        if (!Screen::layoutVertical())
        {
            Screen::setLayoutVertical(true);
        }
        break;
    case Act::None:
        return;
    }
    mKey.clear();
    saveGeneralSettings();
}

SettingsPopup::TapKind SettingsPopup::hit(int x, int y, int &index)
{
    index = -1;
    if (!mController.generalOpen() && !coverageMapOpen())
    {
        return TapKind::None;
    }
    layoutBody();
    const int tab = mWindow.hitTab(x, y);
    if (tab >= 0)
    {
        index = tab;
        return TapKind::Tab;
    }
    const int plate = mWindow.hitPlate(x, y);
    if (plate >= 0 && plate < static_cast<int>(mLines.size()))
    {
        index = mLines[static_cast<size_t>(plate)].id;
        return TapKind::Row;
    }
    const int key = mWindow.hitButton(x, y);
    if (key >= 0 && key < static_cast<int>(mSoft.size()))
    {
        index = key;
        return TapKind::Key;
    }
    if (mWindow.activeTab() == 1 && !mVoicePage && mChartW > 0 && x >= mChartX && y >= mChartY && x < mChartX + mChartW &&
        y < mChartY + mChartH)
    {
        return TapKind::Chart;
    }
    return TapKind::None;
}

void SettingsPopup::activate(TapKind kind, int index, int x, int y)
{
    if (kind == TapKind::Tab)
    {
        if (index < 0 || index >= kPages)
        {
            return;
        }
        if (index != mWindow.activeTab() || mVoicePage)
        {
            mVoicePage = false;
            mVoiceDrag = 0;
            mWindow.setActiveTab(index);
            mKey.clear();
        }
        return;
    }
    if (kind == TapKind::Row)
    {
        if (mVoicePage)
        {
            mVoiceCursor = index;
        }
        else
        {
            const int tab = std::clamp(mWindow.activeTab(), 0, kPages - 1);
            mCursor[tab] = index;
        }
        mKey.clear();
        return;
    }
    if (kind == TapKind::Key)
    {
        if (index < 0 || index >= static_cast<int>(mSoft.size()))
        {
            return;
        }
        apply(mSoft[static_cast<size_t>(index)]);
        return;
    }
    if (kind == TapKind::Chart)
    {
        hitChart(x, y);
    }
}

void SettingsPopup::render()
{
    if (coverageMapOpen())
    {
        const int screenW = std::max(1, mScreen.getWidth());
        const int screenH = std::max(1, mScreen.getHeight());
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        coverageMapRect(x, y, w, h);
        if (w < 8 || h < 8)
        {
            x = 0;
            y = 0;
            w = screenW;
            h = screenH;
        }
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        mCoverageFill.drawRectangle(x, screenH - y - h, w, h, 0x0A0D10FFu);
        mCoverageFill.render();
        glDepthMask(GL_TRUE);
        mEuropeMap.layout(x, y, w, h, screenW, screenH);
        mEuropeMap.render();
        return;
    }
    if (!mController.generalOpen())
    {
        return;
    }

    const int screenW = std::max(1, mScreen.getWidth());
    const int screenH = std::max(1, mScreen.getHeight());
    layoutBody();
    NavVoice &voice = NavVoice::instance();
    std::string voiceKey;
    if (mVoicePage || mWindow.activeTab() == 2)
    {
        for (const std::string &label : voice.voiceLabels())
        {
            voiceKey += label;
            voiceKey.push_back('|');
        }
    }
    const std::string key =
        mTitle + "|" + std::to_string(mWindow.activeTab()) + "|" + std::to_string(mCursor[mWindow.activeTab()]) + "|" +
        std::to_string(mVoiceCursor) + "|" + std::to_string(mVoiceScroll) + "|" + voice.voiceLabel() + "|" +
        (voice.narration() ? "1" : "0") + (voice.airspace() ? "1" : "0") + (voice.reporting() ? "1" : "0") +
        (voice.nearest() ? "1" : "0") + (voice.obstacles() ? "1" : "0") + voiceKey +
        std::to_string(mController.farZoom()) + ":" + std::to_string(mController.farGrid()) + ":" +
        std::to_string(mController.satZoom()) + ":" + std::to_string(mController.nearGrid()) + ":" +
        std::to_string(static_cast<int>(mSession.source())) + (mSession.horizonSet() ? "H" : "h") +
        (mSession.sensors().attitude ? "A" : "a") + (mSession.sensorOn(0) ? "1" : "0") +
        (mSession.sensorOn(1) ? "1" : "0") + (mSession.sensorOn(2) ? "1" : "0") + (mSession.sensorOn(3) ? "1" : "0") +
        (mSession.receiver() ? "R" : "r") + (Screen::layoutVertical() ? "V" : "L") + std::to_string(mWindow.width()) +
        "x" + std::to_string(mWindow.height());
    if (key != mKey)
    {
        mKey = key;
        for (std::string &line : mSensorValue)
        {
            line.clear();
        }
        mWindow.clearContent();
        drawBody(screenH);
    }
    refreshLive(screenH);
    mWindow.render();
    if (!mVoicePage && mWindow.activeTab() == 1)
    {
        mEuropeMap.layout(mChartX, mChartY, mChartW, mChartH, screenW, screenH);
        mEuropeMap.render();
    }
    if (!mVoicePage && mWindow.activeTab() == 5 && mStats != nullptr)
    {
        const int cap = 40;
        mStats->drawDebug(mWindow.contentX(), mWindow.contentY() + cap, mWindow.contentW(),
                          std::max(1, mWindow.contentH() - cap));
    }
}
