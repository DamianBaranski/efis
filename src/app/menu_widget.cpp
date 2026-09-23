/// \file menu_widget.cpp
/// Lays out the column menu and applies the cell that was tapped.
#include "menu_widget.h"
#include "app_controller.h"
#include "settings_popup.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <glm/glm.hpp>

MenuWidget::MenuWidget(Frame &frame, AppController &controller) : IWidget(frame), mController(controller)
{
    mBoxes.reserve(static_cast<size_t>(kItemCount));
    mLabels.reserve(static_cast<size_t>(kItemCount));
    for (int i = 0; i < kItemCount; ++i)
    {
        mBoxes.emplace_back(std::make_unique<Render2D>(frame.screen()));
        mLabels.emplace_back(std::make_unique<Render2D>(frame.screen()));
    }
}

void MenuWidget::setPopup(SettingsPopup &popup)
{
    mPopup = &popup;
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
    if (mVisible && !mLocked && std::chrono::steady_clock::now() >= mHideAt)
    {
        mVisible = false;
    }
}

bool MenuWidget::itemActive(int index) const
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
            return mController.view() == ViewMode::Ahrs;
        }
        if (item.row == 2)
        {
            return mController.view() == ViewMode::ThreeD;
        }
        if (item.row == 3)
        {
            return mController.view() == ViewMode::TwoD;
        }
        return mController.view() == ViewMode::Planning;
    }
    if (item.col == 1)
    {
        if (mController.view() != ViewMode::ThreeD)
        {
            return false;
        }
        if (item.row == 1)
        {
            return mController.mapMode() == MapMode::Satellite;
        }
        return mController.mapMode() == MapMode::Simple;
    }
    if (item.col == 2)
    {
        if (item.row == 1)
        {
            return mController.aipWalls();
        }
        if (item.row == 2)
        {
            return mController.aipText();
        }
        if (item.row == 3)
        {
            return mController.vrpsOn();
        }
        return mController.obstacles();
    }
    if (item.col == 3)
    {
        if (item.row == 1)
        {
            return mController.enrPage() == EnrPage::Charts;
        }
        if (item.row == 2)
        {
            return mController.enrPage() == EnrPage::FlightPlan;
        }
        if (item.row == 3)
        {
            return mController.enrPage() == EnrPage::Nearest;
        }
        return mController.enrPage() == EnrPage::Weather;
    }
    if (item.col == 4)
    {
        if (item.row == 2)
        {
            return mController.generalOpen();
        }
        return mController.statsVisible();
    }
    return false;
}

void MenuWidget::activateItem(int index)
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
            mController.setView(ViewMode::Ahrs);
        }
        else if (item.row == 2)
        {
            mController.setView(ViewMode::ThreeD);
        }
        else if (item.row == 3)
        {
            mController.setView(ViewMode::TwoD);
        }
        else
        {
            mController.setView(ViewMode::Planning);
        }
        return;
    }
    if (item.col == 1)
    {
        if (item.header)
        {
            mController.setMap(mController.mapMode());
            return;
        }
        mController.setMap(item.row == 1 ? MapMode::Satellite : MapMode::Simple);
        return;
    }
    if (item.col == 2)
    {
        mController.toggleAip(item.row);
        return;
    }
    if (item.col == 3)
    {
        if (item.row == 1)
        {
            mController.setEnrPage(EnrPage::Charts);
        }
        else if (item.row == 2)
        {
            mController.setEnrPage(EnrPage::FlightPlan);
        }
        else if (item.row == 3)
        {
            mController.setEnrPage(EnrPage::Nearest);
        }
        else
        {
            mController.setEnrPage(EnrPage::Weather);
        }
        return;
    }
    if (item.col == 4 && item.row == 1)
    {
        mController.toggleStats();
        return;
    }
    if (item.col == 4 && item.row == 2)
    {
        mController.setGeneralOpen(true);
        if (mPopup)
        {
            mPopup->invalidate();
        }
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
        mBoxes[static_cast<size_t>(i)]->drawRectangle(x, glY, mBoxW, mBoxH, fill);
        mLabels[static_cast<size_t>(i)]->drawTextCentered(item.label, static_cast<float>(mFont),
                                                          static_cast<float>(x + mBoxW / 2),
                                                          static_cast<float>(glY + mBoxH / 2), 0xFFFFFFFFu);
    }
}

int MenuWidget::hitMenuItem(int x, int y) const
{
    for (int i = 0; i < kItemCount; ++i)
    {
        const MenuItem &item = kItems[i];
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

bool MenuWidget::mouseClick(int x, int y)
{
    const uint64_t now = SDL_GetTicks64();
    const bool doubleTap = mLastTapMs != 0 && now - mLastTapMs <= kDoubleTapMs;
    mLastTapMs = now;
    if (mController.generalOpen() && mPopup)
    {
        const int tab = mPopup->hitTab(x, y);
        if (tab >= 0)
        {
            if (tab != mPopup->activeTab())
            {
                mPopup->setActiveTab(tab);
            }
            bumpMenuTimeout();
            return true;
        }
        if (mPopup->activeTab() == 0)
        {
            const int control = mPopup->hitButton(x, y);
            if (control >= 0)
            {
                mPopup->adjustRender(control);
                return true;
            }
        }
        if (mPopup->activeTab() == 2)
        {
            const int control = mPopup->hitButton(x, y);
            if (control >= 0)
            {
                mPopup->adjustSound(control);
                return true;
            }
        }
        if (mPopup->hitChart(x, y))
        {
            bumpMenuTimeout();
            return true;
        }
        if (mVisible)
        {
            const int item = hitMenuItem(x, y);
            if (item >= 0 && kItems[item].col == 4 && kItems[item].row == 2)
            {
                mController.setGeneralOpen(false);
                mPopup->invalidate();
                bumpMenuTimeout();
                return true;
            }
        }
        if (mPopup->contains(x, y))
        {
            return true;
        }
        mController.setGeneralOpen(false);
        mPopup->invalidate();
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
