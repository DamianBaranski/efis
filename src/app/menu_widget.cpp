/// \file menu_widget.cpp
/// Lays out the column menu and applies the cell that was tapped.
#include "menu_widget.h"
#include "app_controller.h"
#include "settings_popup.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <glm/glm.hpp>

MenuWidget::MenuWidget(Frame &frame, AppController &controller, SettingsPopup &popup)
    : IWidget(frame), mController(controller), mPopup(popup)
{
    buildCells();
    mBoxes.reserve(mCells.size());
    mLabels.reserve(mCells.size());
    for (size_t i = 0; i < mCells.size(); ++i)
    {
        mBoxes.emplace_back(std::make_unique<Render2D>(frame.screen()));
        mLabels.emplace_back(std::make_unique<Render2D>(frame.screen()));
    }
}

void MenuWidget::buildCells()
{
    const auto off = [] { return false; };
    auto add = [this](const char *label, int col, int row, bool header, std::function<void()> execute,
                      std::function<bool()> active) {
        mCells.push_back(Cell{label, col, row, header, std::move(execute), std::move(active)});
    };
    add("MODE", 0, 0, true, nullptr, off);
    add("AHRS", 0, 1, false, [this] { mController.setView(ViewMode::Ahrs); },
        [this] { return mController.view() == ViewMode::Ahrs; });
    add("3D", 0, 2, false, [this] { mController.setView(ViewMode::ThreeD); },
        [this] { return mController.view() == ViewMode::ThreeD; });
    add("2D", 0, 3, false, [this] { mController.setView(ViewMode::TwoD); },
        [this] { return mController.view() == ViewMode::TwoD; });
    add("PLANNING", 0, 4, false, [this] { mController.setView(ViewMode::Planning); },
        [this] { return mController.view() == ViewMode::Planning; });
    add("MAP", 1, 0, true, [this] { mController.setMap(mController.mapMode()); }, off);
    add("SATT", 1, 1, false, [this] { mController.setMap(MapMode::Satellite); },
        [this] { return mController.view() == ViewMode::ThreeD && mController.mapMode() == MapMode::Satellite; });
    add("SMPL", 1, 2, false, [this] { mController.setMap(MapMode::Simple); },
        [this] { return mController.view() == ViewMode::ThreeD && mController.mapMode() == MapMode::Simple; });
    add("AIP", 2, 0, true, nullptr, off);
    add("3D", 2, 1, false, [this] { mController.toggleAip(1); }, [this] { return mController.aipWalls(); });
    add("TEXT", 2, 2, false, [this] { mController.toggleAip(2); }, [this] { return mController.aipText(); });
    add("VRP", 2, 3, false, [this] { mController.toggleAip(3); }, [this] { return mController.vrpsOn(); });
    add("OBSTCL", 2, 4, false, [this] { mController.toggleAip(4); }, [this] { return mController.obstacles(); });
    add("ENR", 3, 0, true, nullptr, off);
    add("CHRTS", 3, 1, false, [this] { mController.setEnrPage(EnrPage::Charts); },
        [this] { return mController.enrPage() == EnrPage::Charts; });
    add("FLP", 3, 2, false, [this] { mController.setEnrPage(EnrPage::FlightPlan); },
        [this] { return mController.enrPage() == EnrPage::FlightPlan; });
    add("NRST", 3, 3, false, [this] { mController.setEnrPage(EnrPage::Nearest); },
        [this] { return mController.enrPage() == EnrPage::Nearest; });
    add("WTHR", 3, 4, false, [this] { mController.setEnrPage(EnrPage::Weather); },
        [this] { return mController.enrPage() == EnrPage::Weather; });
    add("CONF", 4, 0, true, nullptr, off);
    add("STATS", 4, 1, false, [this] { mController.toggleStats(); }, [this] { return mController.statsVisible(); });
    add("GENERAL", 4, 2, false,
        [this] {
            mController.setGeneralOpen(true);
            mPopup.invalidate();
        },
        [this] { return mController.generalOpen(); });
}

void MenuWidget::showMenu()
{
    mVisible = true;
    mLocked = false;
    bumpMenuTimeout();
    rebuildMenuSprites();
}

void MenuWidget::bumpMenuTimeout()
{
    mHideAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(kMenuTimeoutMs);
}

void MenuWidget::expireMenu()
{
    if (mController.generalOpen())
    {
        mVisible = true;
        return;
    }
    if (mVisible && !mLocked && std::chrono::steady_clock::now() >= mHideAt)
    {
        mVisible = false;
    }
}

void MenuWidget::activateItem(int index)
{
    const Cell &item = mCells[static_cast<size_t>(index)];
    if (item.header && item.col != 1)
    {
        return;
    }
    if (item.execute)
    {
        item.execute();
    }
}

void MenuWidget::layoutMenu()
{
    const int w = std::max(1, mScreen.getWidth());
    const int h = std::max(1, mScreen.getHeight());
    mMenuW = w;
    mMenuH = h;
    mPad = std::max(4, h / 160);
    mGap = std::max(4, w / 220);
    mTop = mPad;
    mBoxH = std::max(36, h / 18);
    mBoxW = std::max(48, (w - 2 * mPad - (kMenuCols - 1) * mGap) / kMenuCols);
    mFont = std::clamp(mBoxH * 2 / 5, 12, 22);
}

void MenuWidget::cellRect(int col, int row, int &x, int &glY) const
{
    x = mPad + col * (mBoxW + mGap);
    const int sdlY = mTop + row * (mBoxH + mGap);
    glY = mMenuH - sdlY - mBoxH;
}

void MenuWidget::rebuildMenuSprites()
{
    layoutMenu();
    mSeenRevision = mController.revision();
    for (int i = 0; i < static_cast<int>(mCells.size()); ++i)
    {
        const Cell &item = mCells[static_cast<size_t>(i)];
        int x = 0;
        int glY = 0;
        cellRect(item.col, item.row, x, glY);
        uint32_t fill = 0xFFFFFF40u;
        if (item.header)
        {
            fill = 0x00000088u;
        }
        else if (item.active && item.active())
        {
            fill = 0x4DA3FFB0u;
        }
        mBoxes[static_cast<size_t>(i)]->drawRectangle(x, glY, mBoxW, mBoxH, fill);
        mLabels[static_cast<size_t>(i)]->drawTextCentered(item.label, static_cast<float>(mFont),
                                                          static_cast<float>(x + mBoxW / 2),
                                                          static_cast<float>(glY + mBoxH / 2), 0xFFFFFFFFu);
    }
}

int MenuWidget::hitMenuItem(int x, int y) const
{
    for (int i = 0; i < static_cast<int>(mCells.size()); ++i)
    {
        const Cell &item = mCells[static_cast<size_t>(i)];
        if (item.header && item.col != 1)
        {
            continue;
        }
        const int left = mPad + item.col * (mBoxW + mGap);
        const int top = mTop + item.row * (mBoxH + mGap);
        if (x >= left && x < left + mBoxW && y >= top && y < top + mBoxH)
        {
            return i;
        }
    }
    return -1;
}

bool MenuWidget::menuContains(int x, int y) const
{
    const int w = kMenuCols * mBoxW + (kMenuCols - 1) * mGap;
    const int h = kMenuRows * mBoxH + (kMenuRows - 1) * mGap;
    return x >= mPad && y >= mTop && x < mPad + w && y < mTop + h;
}

void MenuWidget::render()
{
    expireMenu();
    if (!mVisible)
    {
        return;
    }
    if (mScreen.getWidth() != mMenuW || mScreen.getHeight() != mMenuH || mSeenRevision != mController.revision())
    {
        rebuildMenuSprites();
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const glm::mat4 identity(1.0f);
    for (int i = 0; i < static_cast<int>(mCells.size()); ++i)
    {
        mBoxes[static_cast<size_t>(i)]->setTransformationMatrix(identity);
        mBoxes[static_cast<size_t>(i)]->render();
        mLabels[static_cast<size_t>(i)]->setTransformationMatrix(identity);
        mLabels[static_cast<size_t>(i)]->render();
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

bool MenuWidget::mouseClick(int x, int y)
{
    const uint64_t now = SDL_GetTicks64();
    const bool doubleTap = mLastTapMs != 0 && now - mLastTapMs <= kDoubleTapMs;
    mLastTapMs = now;
    if (mController.generalOpen())
    {
        const int tab = mPopup.hitTab(x, y);
        if (tab >= 0)
        {
            if (tab != mPopup.activeTab())
            {
                mPopup.setActiveTab(tab);
            }
            bumpMenuTimeout();
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
            bumpMenuTimeout();
            return true;
        }
        if (mVisible)
        {
            const int item = hitMenuItem(x, y);
            if (item >= 0 && mCells[static_cast<size_t>(item)].col == 4 && mCells[static_cast<size_t>(item)].row == 2)
            {
                mController.setGeneralOpen(false);
                mPopup.invalidate();
                bumpMenuTimeout();
                return true;
            }
        }
        if (mPopup.contains(x, y))
        {
            return true;
        }
        mController.setGeneralOpen(false);
        mPopup.invalidate();
        bumpMenuTimeout();
        return true;
    }
    if (mVisible)
    {
        if (doubleTap)
        {
            mLocked = true;
            return true;
        }
        const int item = hitMenuItem(x, y);
        if (item >= 0)
        {
            activateItem(item);
            mLocked = false;
            bumpMenuTimeout();
            return true;
        }
        if (mLocked && !menuContains(x, y))
        {
            mVisible = false;
            mLocked = false;
            return true;
        }
        if (!mLocked)
        {
            bumpMenuTimeout();
        }
        return true;
    }
    showMenu();
    return true;
}
