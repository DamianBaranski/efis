/// \file settings_popup.cpp
/// Draws the GENERAL window and edits imagery coverage and narration.
#include "settings_popup.h"
#include "app_controller.h"
#include "nav_voice.h"
#include "terrain_widget.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

SettingsPopup::SettingsPopup(Frame &frame, AppController &controller, TerrainWidget &terrain)
    : IWidget(frame), mController(controller), mTerrain(terrain), mWindow(frame.screen()), mEuropeMap(frame.screen())
{
}

void SettingsPopup::invalidate()
{
    mKey.clear();
}

int SettingsPopup::hitTab(int x, int y) const
{
    return mWindow.hitTab(x, y);
}

int SettingsPopup::activeTab() const
{
    return mWindow.activeTab();
}

void SettingsPopup::setActiveTab(int tab)
{
    mWindow.setActiveTab(tab);
    mKey.clear();
}

int SettingsPopup::hitButton(int x, int y) const
{
    return mWindow.hitButton(x, y);
}

bool SettingsPopup::contains(int x, int y) const
{
    return mWindow.contains(x, y);
}

bool SettingsPopup::hitChart(int x, int y)
{
    return mWindow.activeTab() == 1 && mEuropeMap.hit(x, y);
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
    double lat = mTerrain.cameraLatitude();
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
}

void SettingsPopup::adjustSound(int control)
{
    NavVoice &voice = NavVoice::instance();
    switch (control)
    {
    case 0:
        voice.setNarration(!voice.narration());
        break;
    case 1:
        voice.stepVoice(-1);
        break;
    case 2:
        voice.stepVoice(1);
        break;
    case 3:
        voice.setAirspace(!voice.airspace());
        break;
    case 4:
        voice.setReporting(!voice.reporting());
        break;
    case 5:
        voice.setNearest(!voice.nearest());
        break;
    case 6:
        voice.setObstacles(!voice.obstacles());
        break;
    default:
        voice.preview();
        break;
    }
    mKey.clear();
}

void SettingsPopup::drawSoundPage(int screenH)
{
    NavVoice &voice = NavVoice::instance();
    const int boxX = mWindow.contentX();
    const int boxY = mWindow.contentY();
    const int boxW = mWindow.contentW();
    const int boxH = mWindow.contentH();
    constexpr int kRows = 7;
    const int rowH = std::max(1, boxH / kRows);
    const int btnH = std::clamp(rowH * 2 / 3, 36, 72);
    const int btnW = std::clamp(boxW / 5, 96, 160);
    const float font = static_cast<float>(std::clamp(btnH / 3, 16, 28));
    const char *names[kRows] = {"NARRATION", "VOICE", "AIRSPACE", "REPORTS", "NEAREST", "OBSTACLES", "TEST"};
    const int labelX = boxX + boxW * 22 / 100;
    const int btnX = boxX + boxW - btnW - boxW / 14;
    for (int row = 0; row < kRows; ++row)
    {
        if (row == 1)
        {
            continue;
        }
        const int sdlRowTop = boxY + row * rowH;
        const int btnY = sdlRowTop + (rowH - btnH) / 2;
        const float textY = static_cast<float>(screenH - (sdlRowTop + rowH / 2));
        const std::string nameKey = std::string("efis-sound-name-") + std::to_string(row);
        mWindow.setText(row, names[row], font, static_cast<float>(labelX), textY, nameKey.c_str());
        const char *label = "ON";
        int slot = row;
        if (row == 0)
        {
            label = voice.narration() ? "ON" : "OFF";
        }
        else if (row == 2)
        {
            label = voice.airspace() ? "ON" : "OFF";
            slot = 3;
        }
        else if (row == 3)
        {
            label = voice.reporting() ? "ON" : "OFF";
            slot = 4;
        }
        else if (row == 4)
        {
            label = voice.nearest() ? "ON" : "OFF";
            slot = 5;
        }
        else if (row == 5)
        {
            label = voice.obstacles() ? "ON" : "OFF";
            slot = 6;
        }
        else
        {
            label = "PLAY";
            slot = 7;
        }
        const std::string buttonKey = std::string("efis-sound-btn-") + std::to_string(slot) + "-" + label;
        mWindow.setButton(slot, btnX, btnY, btnW, btnH, label, font, buttonKey.c_str());
    }

    const int voiceTop = boxY + rowH;
    const int voiceY = voiceTop + (rowH - btnH) / 2;
    const float voiceTextY = static_cast<float>(screenH - (voiceTop + rowH / 2));
    const std::string voiceName = voice.voiceLabel();
    const int step = std::min(btnH, 64);
    const int voiceCenter = boxX + boxW * 62 / 100;
    mWindow.setText(1, "VOICE", font, static_cast<float>(labelX), voiceTextY, "efis-sound-name-1");
    mWindow.setText(7, voiceName, font, static_cast<float>(voiceCenter), voiceTextY,
                    (std::string("efis-sound-voice-") + voiceName).c_str());
    mWindow.setButton(1, voiceCenter - step * 3, voiceY, step, btnH, "-", font, "efis-sound-voice-minus");
    mWindow.setButton(2, voiceCenter + step * 2, voiceY, step, btnH, "+", font, "efis-sound-voice-plus");
}

void SettingsPopup::drawPage()
{
    const int screenW = std::max(1, mScreen.getWidth());
    const int screenH = std::max(1, mScreen.getHeight());
    mWindow.setTabLabel(0, "RENDER");
    mWindow.setTabLabel(1, "MAPS");
    mWindow.setTabLabel(2, "SOUND");
    mWindow.layout(screenW, screenH);
    char farDist[16];
    char nearDist[16];
    formatKm(farDist, sizeof(farDist), ringRadiusKm(mController.farZoom(), mController.farGrid()));
    formatKm(nearDist, sizeof(nearDist), ringRadiusKm(mController.satZoom(), mController.nearGrid()));
    char farZoom[8];
    char nearZoom[8];
    std::snprintf(farZoom, sizeof(farZoom), "Z%d", mController.farZoom());
    std::snprintf(nearZoom, sizeof(nearZoom), "Z%d", mController.satZoom());
    const NavVoice &voice = NavVoice::instance();
    const std::string sound = std::string(voice.narration() ? "1" : "0") + (voice.airspace() ? "1" : "0") +
                              (voice.reporting() ? "1" : "0") + (voice.nearest() ? "1" : "0") +
                              (voice.obstacles() ? "1" : "0") + NavVoice::instance().voiceLabel();
    const std::string key = std::to_string(mWindow.activeTab()) + farDist + farZoom + nearDist + nearZoom + sound +
                            std::to_string(mWindow.width()) + std::to_string(mWindow.height());
    if (key != mKey)
    {
        mKey = key;
        mWindow.clearContent();
        if (mWindow.activeTab() == 0)
        {
            const int boxX = mWindow.contentX();
            const int boxY = mWindow.contentY();
            const int boxW = mWindow.contentW();
            const int boxH = mWindow.contentH();
            const int rowH = boxH / 2;
            const int btn = std::clamp(std::min(rowH, boxW / 8) * 2 / 3, 48, 96);
            const float font = static_cast<float>(std::clamp(btn / 2, 18, 36));
            const char *names[2] = {"FAR", "NEAR"};
            const char *dists[2] = {farDist, nearDist};
            const char *zooms[2] = {farZoom, nearZoom};
            static const char *kNameKeys[2] = {"efis-render-far", "efis-render-near"};
            static const char *kDistKeys[2] = {"efis-render-fard", "efis-render-neard"};
            static const char *kZoomKeys[2] = {"efis-render-farz", "efis-render-nearz"};
            for (int row = 0; row < 2; ++row)
            {
                const int sdlRowTop = boxY + row * rowH;
                const int btnY = sdlRowTop + (rowH - btn) / 2;
                const int sdlMid = sdlRowTop + rowH / 2;
                const float textY = static_cast<float>(screenH - sdlMid);
                const int labelX = boxX + boxW / 10;
                const int distCenter = boxX + boxW * 42 / 100;
                const int zoomCenter = boxX + boxW * 78 / 100;
                const int distGap = btn + btn / 2;
                const int zoomGap = btn;
                mWindow.setText(row * 3, names[row], font, static_cast<float>(labelX), textY, kNameKeys[row]);
                mWindow.setText(1 + row * 3, dists[row], font, static_cast<float>(distCenter), textY, kDistKeys[row]);
                mWindow.setText(2 + row * 3, zooms[row], font, static_cast<float>(zoomCenter), textY, kZoomKeys[row]);
                const int base = row * 4;
                mWindow.setButton(base + 0, distCenter - distGap - btn, btnY, btn, btn, "-", font, "efis-pop-minus");
                mWindow.setButton(base + 1, distCenter + distGap, btnY, btn, btn, "+", font, "efis-pop-plus");
                mWindow.setButton(base + 2, zoomCenter - zoomGap - btn, btnY, btn, btn, "-", font, "efis-pop-minus");
                mWindow.setButton(base + 3, zoomCenter + zoomGap, btnY, btn, btn, "+", font, "efis-pop-plus");
            }
        }
        else if (mWindow.activeTab() == 2)
        {
            drawSoundPage(screenH);
        }
    }
    mWindow.render();
    if (mWindow.activeTab() == 1)
    {
        mEuropeMap.layout(mWindow.contentX(), mWindow.contentY(), mWindow.contentW(), mWindow.contentH(), screenW,
                          screenH);
        mEuropeMap.render();
    }
}

void SettingsPopup::render()
{
    if (!mController.generalOpen())
    {
        return;
    }
    drawPage();
}
