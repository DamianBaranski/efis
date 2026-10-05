/// \file menu_widget.cpp
/// Bookmark on the right edge and the full-height side menu it opens.
#include "menu_widget.h"
#include "app_controller.h"
#include "settings_popup.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace
{
constexpr uint32_t kPanel = 0x101820B8u;
constexpr uint32_t kTab = 0x101820E8u;
constexpr uint32_t kTabOn = 0x4DA3FFD0u;
constexpr uint32_t kHeader = 0x00000088u;
constexpr uint32_t kRow = 0x1A2430B0u;
constexpr uint32_t kRowOn = 0x4DA3FFC0u;
constexpr uint32_t kInk = 0xFFFFFFFFu;
constexpr int kSlopPx = 14;
}

MenuWidget::MenuWidget(Frame &frame, AppController &controller, SettingsPopup &popup)
    : IWidget(frame), mController(controller), mPopup(popup)
{
    buildEntries();
}

void MenuWidget::buildEntries()
{
    auto header = [this](const char *label) {
        Entry row;
        row.label = label;
        row.header = true;
        mRows.push_back(std::move(row));
    };
    auto choice = [this](const char *label, std::function<void()> run, std::function<bool()> selected) {
        Entry row;
        row.label = label;
        row.run = std::move(run);
        row.selected = std::move(selected);
        mRows.push_back(std::move(row));
    };

    header("PICTURE");
    choice("AHRS", [this] {
        if (mController.picture() != ViewMode::Ahrs)
        {
            mController.setPicture(ViewMode::Ahrs);
        }
    }, [this] { return mController.picture() == ViewMode::Ahrs; });
    choice("3D", [this] {
        if (mController.picture() != ViewMode::ThreeD)
        {
            mController.setPicture(ViewMode::ThreeD);
        }
    }, [this] { return mController.picture() == ViewMode::ThreeD; });
    choice("SPLIT", [this] { mController.setSplit(!mController.split()); }, [this] { return mController.split(); });
    mRows.back().textOf = [this] { return mController.split() ? std::string("SPLIT ON") : std::string("SPLIT OFF"); };

    header("TERRAIN");
    choice("SATELLITE", [this] {
        if (mController.mapMode() != MapMode::Satellite)
        {
            mController.setTerrainPicture(MapMode::Satellite);
        }
    }, [this] { return mController.mapMode() == MapMode::Satellite; });
    choice("SIMPLE", [this] {
        if (mController.mapMode() != MapMode::Simple)
        {
            mController.setTerrainPicture(MapMode::Simple);
        }
    }, [this] { return mController.mapMode() == MapMode::Simple; });

    header("LAYERS");
    choice("WALLS", [this] { mController.setAipWalls(!mController.aipWalls()); },
           [this] { return mController.aipWalls(); });
    mRows.back().textOf = [this] {
        return mController.aipWalls() ? std::string("WALLS ON") : std::string("WALLS OFF");
    };
    choice("LABELS", [this] { mController.setAipLabels(!mController.aipText()); },
           [this] { return mController.aipText(); });
    mRows.back().textOf = [this] {
        return mController.aipText() ? std::string("LABELS ON") : std::string("LABELS OFF");
    };
    choice("POINTS", [this] { mController.setVrpsOn(!mController.vrpsOn()); }, [this] { return mController.vrpsOn(); });
    mRows.back().textOf = [this] {
        return mController.vrpsOn() ? std::string("POINTS ON") : std::string("POINTS OFF");
    };
    choice("OBSTACLES", [this] { mController.setObstaclesOn(!mController.obstacles()); },
           [this] { return mController.obstacles(); });
    mRows.back().textOf = [this] {
        return mController.obstacles() ? std::string("OBSTACLES ON") : std::string("OBSTACLES OFF");
    };

    header("SETUP");
    choice("STATS", [this] { mController.toggleStats(); }, [this] { return mController.statsVisible(); });
    mRows.back().textOf = [this] {
        return mController.statsVisible() ? std::string("STATS ON") : std::string("STATS OFF");
    };
    choice("GENERAL", [this] {
        mOpen = false;
        mController.setGeneralOpen(true);
        mPopup.invalidate();
    }, [this] { return mController.generalOpen(); });
    choice("PLANNING", [this] {
        if (mController.view() != ViewMode::Planning)
        {
            mOpen = false;
            mController.setView(ViewMode::Planning);
        }
    }, [this] { return mController.view() == ViewMode::Planning; });
}

void MenuWidget::measure()
{
    mScreenW = std::max(1, mScreen.getWidth());
    mScreenH = std::max(1, mScreen.getHeight());
    mTabW = 80;
    mTabH = 96;
    mTabY = 12;
    mPanelW = std::min(440, std::max(300, mScreenW * 2 / 5));
    mPanelX = 0;
    mPanelY = 0;
    mPanelH = mScreenH;
    mTabX = mOpen ? mPanelW : 0;

    int headers = 0;
    int items = 0;
    for (const Entry &row : mRows)
    {
        if (row.header)
        {
            ++headers;
        }
        else
        {
            ++items;
        }
    }
    int headerH = 40;
    int itemH = 72;
    int content = headers * headerH + items * itemH;
    if (content < mScreenH && items > 0)
    {
        itemH = std::min(88, (mScreenH - headers * headerH) / items);
        content = headers * headerH + items * itemH;
    }
    mContentH = content;
    mScroll = std::clamp(mScroll, 0, std::max(0, mContentH - mPanelH));
    int y = -mScroll;
    for (Entry &row : mRows)
    {
        row.x = mPanelX;
        row.y = y;
        row.w = mPanelW;
        row.h = row.header ? headerH : itemH;
        y += row.h;
    }
    mFont = std::clamp(itemH * 2 / 5, 22, 32);
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
            const int glY = mScreenH - row.y - row.h;
            uint32_t fill = kRow;
            if (row.header)
            {
                fill = kHeader;
            }
            else if (row.selected && row.selected())
            {
                fill = kRowOn;
            }
            sprite().drawRectangle(row.x, glY, row.w, row.h, fill);
            const std::string text = row.textOf ? row.textOf() : row.label;
            sprite().drawTextCentered(text, static_cast<float>(row.header ? std::max(16, mFont - 6) : mFont),
                                      static_cast<float>(row.x + row.w / 2), static_cast<float>(glY + row.h / 2), kInk,
                                      text);
        }
        mContentSprites = static_cast<int>(mSpriteCursor);
    }
    if (!mOpen)
    {
        const int stubGl = mScreenH - mTabY - mTabH;
        sprite().drawRectangle(-mTabW, stubGl, mTabW, mTabH, kTab);
    }
    const int tabGl = mScreenH - mTabY - mTabH;
    sprite().drawRectangle(mTabX, tabGl, mTabW, mTabH, mOpen ? kTabOn : kTab);
    const int steps = 8;
    const int arm = 26;
    const int thick = 8;
    const int cx = mTabX + mTabW / 2;
    const int cy = mTabY + mTabH / 2;
    const int tip = mOpen ? -arm / 2 : arm / 2;
    const int tail = mOpen ? arm / 2 : -arm / 2;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int px = tail + static_cast<int>(std::lround(t * static_cast<float>(tip - tail)));
        const int py = static_cast<int>(std::lround((1.0f - t) * static_cast<float>(arm)));
        sprite().drawRectangle(cx + px - thick / 2, mScreenH - (cy - py) - thick, thick, thick, kInk);
        sprite().drawRectangle(cx + px - thick / 2, mScreenH - (cy + py) - thick, thick, thick, kInk);
    }
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
    if (mController.view() == ViewMode::Planning)
    {
        close();
        return false;
    }
    measure();
    if (inside(x, y, mTabX, mTabY, mTabW, mTabH))
    {
        if (mController.generalOpen())
        {
            mController.setGeneralOpen(false);
            mPopup.invalidate();
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
        const int tab = mPopup.hitTab(x, y);
        if (tab >= 0)
        {
            if (tab != mPopup.activeTab())
            {
                mPopup.setActiveTab(tab);
            }
            return true;
        }
        if (mPopup.activeTab() == 0)
        {
            const int control = mPopup.hitButton(x, y);
            if (control >= 0)
            {
                mPopup.adjustRender(control);
                return true;
            }
        }
        if (mPopup.activeTab() == 2)
        {
            if (mPopup.handleVoice(x, y))
            {
                return true;
            }
            const int control = mPopup.hitButton(x, y);
            if (control >= 0)
            {
                mPopup.adjustSound(control);
                return true;
            }
        }
        if (mPopup.activeTab() == 3)
        {
            if (mPopup.handleSource(x, y))
            {
                return true;
            }
        }
        if (mPopup.activeTab() == 4)
        {
            const int control = mPopup.hitButton(x, y);
            if (control >= 0)
            {
                mPopup.adjustApp(control);
                return true;
            }
        }
        if (mPopup.hitChart(x, y))
        {
            return true;
        }
        if (mPopup.contains(x, y))
        {
            return true;
        }
        mController.setGeneralOpen(false);
        mPopup.invalidate();
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
    (void)x;
    (void)dx;
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
    if (!mOpen || !inside(x, y, mPanelX, mPanelY, mPanelW, mPanelH))
    {
        return false;
    }
    const int maxScroll = std::max(0, mContentH - mPanelH);
    mScroll = std::clamp(mScroll - dy * 72, 0, maxScroll);
    return true;
}

bool MenuWidget::mouseUp(int x, int y)
{
    if (mArm == Arm::None)
    {
        return false;
    }
    const Arm arm = mArm;
    const int index = mArmIndex;
    const bool fire = near(x, y, mArmX, mArmY);
    clearArm();
    if (fire)
    {
        commit(arm, index);
    }
    return true;
}

void MenuWidget::gesturePinchBegan()
{
    clearArm();
}
