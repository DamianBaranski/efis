/// \file menu_widget.cpp
/// Bookmark on the left edge and the full-screen menu it opens.
#include "menu_widget.h"
#include "app_controller.h"
#include "asset_path.h"
#include "general_settings.h"
#include "settings_popup.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace
{
constexpr uint32_t kPanel = 0x101820B8u;
constexpr uint32_t kTab = 0x10182075u;
constexpr uint32_t kTabOn = 0x15203075u;
constexpr uint32_t kTile = 0x131920F0u;
constexpr uint32_t kEdge = 0x273444FFu;
constexpr uint32_t kEdgeOn = 0x39FF6AFFu;
constexpr uint32_t kInk = 0xFFFFFFFFu;
constexpr uint32_t kMuted = 0xA89988FFu;
constexpr int kSlopPx = 14;

/// Square on the left of a tile. The gap to the top, bottom, and left edges is the same.
void iconPane(int tileH, int &inset, int &pane)
{
    inset = std::max(16, tileH / 10);
    pane = std::max(1, tileH - inset * 2);
}
}

MenuWidget::MenuWidget(Frame &frame, AppController &controller, SettingsPopup &popup)
    : IWidget(frame), mController(controller), mPopup(popup)
{
    buildEntries();
}

void MenuWidget::buildEntries()
{
    auto header = [this](const char *title) {
        Entry row;
        row.label = title;
        row.header = true;
        mRows.push_back(std::move(row));
    };
    auto tile = [this](const char *label, const char *detail, const char *icon, bool stateLine,
                       std::function<void()> run, std::function<bool()> selected) {
        Entry row;
        row.label = label;
        row.detail = detail;
        row.icon = icon;
        row.stateLine = stateLine;
        row.run = std::move(run);
        row.selected = std::move(selected);
        mRows.push_back(std::move(row));
    };

    header("PRIMARY INSTRUMENT");
    tile("PLANNING MODE", "Route, direct-to, and send to the panel", "planning", false,
         [this] {
             if (mController.view() != ViewMode::Planning)
             {
                 mOpen = false;
                 mController.setView(ViewMode::Planning);
             }
         },
         [this] { return mController.view() == ViewMode::Planning; });
    tile("AHRS + PLAN", "Map beside the picture", "split", true, [this] { mController.setSplit(!mController.split()); },
         [this] { return mController.split(); });
    tile("AHRS ONLY", "Attitude, heading, and the tapes", "ahrs", false,
         [this] {
             if (mController.picture() != ViewMode::Ahrs)
             {
                 mController.setPicture(ViewMode::Ahrs);
             }
         },
         [this] { return mController.picture() == ViewMode::Ahrs; });
    tile("AHRS + GEO", "Terrain ahead of the aircraft", "three-d", false,
         [this] {
             if (mController.picture() != ViewMode::ThreeD)
             {
                 mController.setPicture(ViewMode::ThreeD);
             }
         },
         [this] { return mController.picture() == ViewMode::ThreeD; });

    header("3D TERRAIN FEATURES");
    tile("SATELLITE", "Photo ground on the 3D view", "satellite", false,
         [this] {
             if (mController.mapMode() != MapMode::Satellite)
             {
                 mController.setTerrainPicture(MapMode::Satellite);
             }
         },
         [this] { return mController.mapMode() == MapMode::Satellite; });
    tile("SIMPLE", "Shaded ground on the 3D view", "simple", false,
         [this] {
             if (mController.mapMode() != MapMode::Simple)
             {
                 mController.setTerrainPicture(MapMode::Simple);
             }
         },
         [this] { return mController.mapMode() == MapMode::Simple; });

    header("3D AIRSPACE FEATURES");
    tile("WALLS", "Airspace volumes in the 3D view", "walls", true,
         [this] { mController.setAipWalls(!mController.aipWalls()); }, [this] { return mController.aipWalls(); });
    tile("LABELS", "Airspace names in the 3D view", "labels", true,
         [this] { mController.setAipLabels(!mController.aipText()); }, [this] { return mController.aipText(); });
    tile("POINTS", "Reporting points in the 3D view", "points", true,
         [this] { mController.setVrpsOn(!mController.vrpsOn()); }, [this] { return mController.vrpsOn(); });
    tile("OBSTACLES", "Towers in the 3D view", "obstacles", true,
         [this] { mController.setObstaclesOn(!mController.obstacles()); }, [this] { return mController.obstacles(); });

    header("SYSTEM SETUP");
    tile("GENERAL", "Imagery, sound, sensors, and layout", "general", false,
         [this] {
             mOpen = false;
             openGeneralSettings();
         },
         [this] { return mController.generalOpen() || coverageMapOpen(); });
}

void MenuWidget::measure()
{
    mScreenW = std::max(1, mScreen.getWidth());
    mScreenH = std::max(1, mScreen.getHeight());
    // Square, 15% larger than the old 96px height.
    mTabW = 96 * 115 / 100;
    mTabH = mTabW;
    mTabY = 12;
    mTabX = 0;
    mPanelX = 0;
    mPanelY = 0;
    mPanelW = mScreenW;
    mPanelH = mScreenH;

    const int pad = 16;
    const int gap = 12;
    const int headerH = std::clamp(mScreenH / 16, 64, 96) * 70 / 100;
    const int top = mTabY + mTabH + 16;
    const int innerW = std::max(1, mScreenW - pad * 2);
    const int tileW = std::max(1, (innerW - gap) / 2);

    auto place = [&](int tileH, bool commit) {
        int y = top;
        int i = 0;
        const int n = static_cast<int>(mRows.size());
        while (i < n)
        {
            if (mRows[static_cast<size_t>(i)].header)
            {
                if (commit)
                {
                    Entry &row = mRows[static_cast<size_t>(i)];
                    row.x = pad;
                    row.y = y - mScroll;
                    row.w = innerW;
                    row.h = headerH;
                }
                y += headerH + 4;
                ++i;
                continue;
            }
            int count = 0;
            while (i + count < n && !mRows[static_cast<size_t>(i + count)].header)
            {
                ++count;
            }
            int col = 0;
            for (int k = 0; k < count; ++k)
            {
                if (commit)
                {
                    Entry &row = mRows[static_cast<size_t>(i + k)];
                    row.x = pad + col * (tileW + gap);
                    row.y = y - mScroll;
                    row.w = tileW;
                    row.h = tileH;
                }
                ++col;
                if (col >= 2)
                {
                    col = 0;
                    y += tileH + gap;
                }
            }
            if (col != 0)
            {
                y += tileH + gap;
            }
            i += count;
        }
        return y + pad;
    };

    // Two columns. A leftover tile stays one column wide. The page scrolls.
    const int rowH = std::max(336, std::clamp(mScreenH / 3, 280, 420)) * 70 / 100;
    const int tileH = rowH * 85 / 100;
    mContentH = place(tileH, false);
    mScroll = std::clamp(mScroll, 0, std::max(0, mContentH - mScreenH));
    place(tileH, true);
    int inset = 0;
    int pane = 0;
    iconPane(tileH, inset, pane);
    const int textBudget = std::max(80, tileW - inset - pane - inset * 2);
    // B612 Mono is about 0.6 em wide. The longest description is ~40 columns.
    const int detail = std::clamp(textBudget * 10 / (40 * 6), 28, 48) * 70 / 100;
    const int name = std::clamp(std::max(detail + 14, rowH / 6), 34, 54);
    mDetailFont = detail * 120 / 100;
    mFont = name * 120 / 100;
    mHeaderFont = std::max(22, headerH / 2) * 115 / 100;
}

bool MenuWidget::inside(int x, int y, int rx, int ry, int rw, int rh) const
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

int MenuWidget::hitRow(int x, int y) const
{
    for (int i = 0; i < static_cast<int>(mRows.size()); ++i)
    {
        const Entry &row = mRows[static_cast<size_t>(i)];
        if (row.header || !row.run)
        {
            continue;
        }
        if (row.y + row.h <= 0 || row.y >= mPanelH)
        {
            continue;
        }
        if (inside(x, y, row.x, row.y, row.w, row.h))
        {
            return i;
        }
    }
    return -1;
}

bool MenuWidget::near(int x, int y, int ax, int ay)
{
    const int dx = x - ax;
    const int dy = y - ay;
    return dx * dx + dy * dy <= kSlopPx * kSlopPx;
}

void MenuWidget::clearArm()
{
    mArm = Arm::None;
    mArmIndex = -1;
    mDragList = false;
    mDragGeneral = false;
}

void MenuWidget::close()
{
    mOpen = false;
    clearArm();
}

void MenuWidget::commit(Arm arm, int index)
{
    if (arm == Arm::Toggle)
    {
        if (mOpen)
        {
            mOpen = false;
            return;
        }
        mOpen = true;
        if (mOnOpen)
        {
            mOnOpen();
        }
        return;
    }
    if (arm == Arm::Row && index >= 0 && index < static_cast<int>(mRows.size()))
    {
        const Entry &row = mRows[static_cast<size_t>(index)];
        if (row.run)
        {
            row.run();
        }
        return;
    }
    if (arm == Arm::Dismiss)
    {
        mOpen = false;
    }
}

Render2D &MenuWidget::sprite()
{
    if (mSpriteCursor >= mSprites.size())
    {
        mSprites.emplace_back(std::make_unique<Render2D>(mScreen));
    }
    return *mSprites[mSpriteCursor++];
}

void MenuWidget::paint()
{
    mSpriteCursor = 0;
    if (mOpen)
    {
        sprite().drawRectangle(mPanelX, 0, mPanelW, mPanelH, kPanel);
        for (const Entry &row : mRows)
        {
            if (row.h <= 0 || row.y + row.h <= 0 || row.y >= mPanelH)
            {
                continue;
            }
            if (row.header)
            {
                const float y = static_cast<float>(mScreenH - row.y - row.h / 2 - mHeaderFont / 3);
                const std::string key = std::string("menu-head-") + row.label;
                sprite().drawText(row.label, static_cast<float>(mHeaderFont), static_cast<float>(row.x), y, kMuted,
                                  key);
                continue;
            }
            const int glY = mScreenH - row.y - row.h;
            const bool on = row.selected && row.selected();
            sprite().drawFramedRectangle(row.x, glY, row.w, row.h, kTile, on ? kEdgeOn : kEdge, 2);
            int inset = 0;
            int pane = 0;
            iconPane(row.h, inset, pane);
            const int paneX = row.x + inset;
            const int paneY = glY + inset;
            if (row.icon != nullptr)
            {
                Render2D &mark = sprite();
                if (on)
                {
                    mark.setColorScale(0.224f, 1.0f, 0.416f);
                }
                else
                {
                    mark.setColorScale(0.941f, 0.957f, 0.973f);
                }
                const std::string path = AssetPath::resolve(std::string("resources/textures/ui/icons/") + row.icon + ".png");
                mark.drawTexture(path, paneX, paneY, pane, pane);
            }
            std::string detail = row.detail;
            const int textX = row.x + inset + (row.icon != nullptr ? pane + inset : 0);
            const int block = mFont + 18 + mDetailFont;
            const float detailY = static_cast<float>(glY + (row.h - block) / 2);
            const float titleY = detailY + static_cast<float>(mDetailFont + 18);
            const std::string titleKey = std::string("menu-title-") + row.label + (on ? "-on" : "-off");
            const std::string detailKey = std::string("menu-detail-") + row.label;
            sprite().drawText(row.label, static_cast<float>(mFont), static_cast<float>(textX), titleY, kInk, titleKey);
            sprite().drawText(detail, static_cast<float>(mDetailFont), static_cast<float>(textX), detailY, kMuted,
                              detailKey);
        }
        mContentSprites = static_cast<int>(mSpriteCursor);
    }
    const bool back = mOpen || mController.generalOpen() || coverageMapOpen();
    const int tabGl = mScreenH - mTabY - mTabH;
    sprite().drawRectangle(mTabX, tabGl, mTabW, mTabH, back ? kTabOn : kTab);
    const int icon = mTabH;
    const char *glyph = back ? "chevron-left" : "chevron-right";
    sprite().drawTexture(AssetPath::resolve(std::string("resources/textures/ui/icons/") + glyph + ".png"),
                         mTabX + (mTabW - icon) / 2, tabGl + (mTabH - icon) / 2, icon, icon);
    mSpriteCount = static_cast<int>(mSpriteCursor);
}

void MenuWidget::ensureSprites()
{
    measure();
    const bool same = mBuiltW == mScreenW && mBuiltH == mScreenH && mBuiltRevision == mController.revision() &&
                      mBuiltOpen == mOpen && mBuiltScroll == mScroll;
    if (same && mSpriteCount > 0)
    {
        return;
    }
    paint();
    mBuiltW = mScreenW;
    mBuiltH = mScreenH;
    mBuiltRevision = mController.revision();
    mBuiltOpen = mOpen;
    mBuiltScroll = mScroll;
}

void MenuWidget::render()
{
    if (mController.view() == ViewMode::Planning)
    {
        close();
        return;
    }
    ensureSprites();
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const glm::mat4 identity(1.0f);
    const int clipped = mOpen ? std::min(mContentSprites, mSpriteCount) : 0;
    if (clipped > 0)
    {
        glEnable(GL_SCISSOR_TEST);
        glScissor(mPanelX, 0, mPanelW, mScreenH);
    }
    for (int i = 0; i < mSpriteCount; ++i)
    {
        if (i == clipped)
        {
            glDisable(GL_SCISSOR_TEST);
        }
        mSprites[static_cast<size_t>(i)]->setTransformationMatrix(identity);
        mSprites[static_cast<size_t>(i)]->render();
    }
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

bool MenuWidget::mouseClick(int x, int y)
{
    measure();
    if (coverageMapOpen())
    {
        if (inside(x, y, mTabX, mTabY, mTabW, mTabH))
        {
            closeCoverageMap();
            clearArm();
            return true;
        }
        mPopup.hitChart(x, y);
        return true;
    }
    if (mController.view() == ViewMode::Planning)
    {
        close();
        return false;
    }
    if (inside(x, y, mTabX, mTabY, mTabW, mTabH))
    {
        if (mController.generalOpen())
        {
            if (!mPopup.handleBack())
            {
                mController.setGeneralOpen(false);
                mPopup.invalidate();
            }
            clearArm();
            return true;
        }
        mArm = Arm::Toggle;
        mArmIndex = -1;
        mArmX = x;
        mArmY = y;
        return true;
    }
    if (mController.generalOpen())
    {
        mDragGeneral = true;
        int index = -1;
        const SettingsPopup::TapKind kind = mPopup.hit(x, y, index);
        mArmIndex = index;
        mArmX = x;
        mArmY = y;
        if (kind == SettingsPopup::TapKind::Tab)
        {
            mArm = Arm::GeneralTab;
        }
        else if (kind == SettingsPopup::TapKind::Row)
        {
            mArm = Arm::GeneralRow;
        }
        else if (kind == SettingsPopup::TapKind::Key)
        {
            mArm = Arm::GeneralKey;
        }
        else if (kind == SettingsPopup::TapKind::Chart)
        {
            mArm = Arm::GeneralChart;
        }
        else
        {
            mArm = Arm::None;
        }
        return true;
    }
    if (mOpen && inside(x, y, mPanelX, mPanelY, mPanelW, mPanelH))
    {
        mDragList = true;
        const int row = hitRow(x, y);
        if (row >= 0)
        {
            mArm = Arm::Row;
            mArmIndex = row;
            mArmX = x;
            mArmY = y;
        }
        else
        {
            mArm = Arm::None;
            mArmIndex = -1;
        }
        return true;
    }
    if (mOpen)
    {
        mArm = Arm::Dismiss;
        mArmIndex = -1;
        mArmX = x;
        mArmY = y;
        return true;
    }
    return false;
}

bool MenuWidget::mouseMove(int x, int y, int dx, int dy)
{
    (void)dx;
    if (mController.generalOpen() && mDragGeneral)
    {
        if (mArm != Arm::None && !near(x, y, mArmX, mArmY))
        {
            mArm = Arm::None;
            mArmIndex = -1;
        }
        mPopup.scrollBy(dy);
        (void)y;
        return true;
    }
    if (!mOpen || !mDragList)
    {
        return false;
    }
    const int maxScroll = std::max(0, mContentH - mPanelH);
    mScroll = std::clamp(mScroll - dy, 0, maxScroll);
    (void)y;
    return true;
}

bool MenuWidget::mouseWheel(int x, int y, int dy)
{
    if (mController.generalOpen() || coverageMapOpen())
    {
        mPopup.scrollBy(dy > 0 ? 64 : -64);
        return true;
    }
    if (!mOpen || !inside(x, y, mPanelX, mPanelY, mPanelW, mPanelH))
    {
        return false;
    }
    const int maxScroll = std::max(0, mContentH - mPanelH);
    mScroll = std::clamp(mScroll - dy * 160, 0, maxScroll);
    return true;
}

bool MenuWidget::mouseUp(int x, int y)
{
    const bool generalDrag = mDragGeneral;
    if (mArm == Arm::None)
    {
        clearArm();
        return generalDrag;
    }
    const Arm arm = mArm;
    const int index = mArmIndex;
    const bool fire = near(x, y, mArmX, mArmY);
    clearArm();
    if (!fire)
    {
        return true;
    }
    if (arm == Arm::GeneralTab)
    {
        mPopup.activate(SettingsPopup::TapKind::Tab, index, x, y);
    }
    else if (arm == Arm::GeneralRow)
    {
        mPopup.activate(SettingsPopup::TapKind::Row, index, x, y);
    }
    else if (arm == Arm::GeneralKey)
    {
        mPopup.activate(SettingsPopup::TapKind::Key, index, x, y);
    }
    else if (arm == Arm::GeneralChart)
    {
        mPopup.activate(SettingsPopup::TapKind::Chart, index, x, y);
    }
    else
    {
        commit(arm, index);
    }
    return true;
}

void MenuWidget::gesturePinchBegan()
{
    clearArm();
}
