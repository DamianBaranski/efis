/// \file tab_window.h
/// Settings window with a tab strip and a page of labels and buttons.
#ifndef TAB_WINDOW_H
#define TAB_WINDOW_H

#include "render2d.h"
#include "screen.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

/// Tabbed settings window used by GENERAL and later popups.
/// Left column: stacked tabs. Active tab is light gray. Right side is the page.
/// SDL coordinates have the origin at the top left. Drawing uses GL, origin at the bottom left.
template <int kTabs, int kTexts, int kButtons>
class TabWindow
{
public:
    /// Allocates the tab strip and the page drawables. Call layout before use.
    /// \param screen Window whose height converts SDL coordinates to GL.
    explicit TabWindow(Screen &screen) : mScreen(screen)
    {
        mBox = std::make_unique<Render2D>(screen);
        mTabStrip = std::make_unique<Render2D>(screen);
        for (int i = 0; i < kTabs; ++i)
        {
            mTabBoxes[static_cast<size_t>(i)] = std::make_unique<Render2D>(screen);
            mTabLabels[static_cast<size_t>(i)] = std::make_unique<Render2D>(screen);
        }
        for (int i = 0; i < kTexts; ++i)
        {
            mTexts[static_cast<size_t>(i)] = std::make_unique<Render2D>(screen);
        }
        for (int i = 0; i < kButtons; ++i)
        {
            mButtonBoxes[static_cast<size_t>(i)] = std::make_unique<Render2D>(screen);
            mButtonLabels[static_cast<size_t>(i)] = std::make_unique<Render2D>(screen);
        }
    }

    /// Sizes the window and the tab strip. Call again when the screen size changes.
    /// \param screenW Window width in pixels.
    /// \param screenH Window height in pixels.
    /// \param topLimit SDL Y the window must start at or below. Zero centres it.
    void layout(int screenW, int screenH, int topLimit = 0)
    {
        mScreenW = std::max(1, screenW);
        mScreenH = std::max(1, screenH);
        const int margin = std::max(16, mScreenH / 30);
        const int top = std::max(margin, topLimit);
        const int availH = std::max(1, mScreenH - top - margin);
        mBoxW = std::min(mScreenW - 2 * margin, std::max(720, mScreenW * 3 / 4));
        mBoxH = std::min(availH, std::max(280, mScreenH * 3 / 5));
        mSdlX = (mScreenW - mBoxW) / 2;
        mSdlY = top + std::max(0, (availH - mBoxH) / 2);
        mTabPad = std::max(8, mBoxH / 50);
        mTabGap = std::max(6, mBoxH / 80);
        mTabW = std::clamp(mBoxW / 5, 140, 240);
        const int inner = mBoxH - 2 * mTabPad - (kTabs - 1) * mTabGap;
        mTabH = std::min(88, std::max(1, inner / std::max(1, kTabs)));
        mContentX = mSdlX + mTabW;
        mContentY = mSdlY;
        mContentW = mBoxW - mTabW;
        mContentH = mBoxH;
        rebuildTabs();
    }

    /// Left edge of the window, SDL pixels.
    int sdlX() const { return mSdlX; }
    /// Top edge of the window, SDL pixels.
    int sdlY() const { return mSdlY; }
    /// Window width in pixels.
    int width() const { return mBoxW; }
    /// Window height in pixels.
    int height() const { return mBoxH; }
    /// Screen height in pixels. Used to flip SDL Y into GL Y.
    int screenHeight() const { return mScreenH; }
    /// Left edge of the page, to the right of the tab strip. SDL pixels.
    int contentX() const { return mContentX; }
    /// Top edge of the page, SDL pixels.
    int contentY() const { return mContentY; }
    /// Page width in pixels.
    int contentW() const { return mContentW; }
    /// Page height in pixels.
    int contentH() const { return mContentH; }
    /// Index of the light-gray tab.
    int activeTab() const { return mActiveTab; }

    /// Text drawn on one tab. Null is ignored.
    void setTabLabel(int slot, const char *label)
    {
        if (slot < 0 || slot >= kTabs || label == nullptr)
        {
            return;
        }
        mTabNames[static_cast<size_t>(slot)] = label;
    }

    /// Highlights one tab and leaves the page contents for the caller to refill.
    void setActiveTab(int slot)
    {
        if (slot < 0 || slot >= kTabs)
        {
            return;
        }
        mActiveTab = slot;
        rebuildTabs();
    }

    /// True when the SDL point is inside the window, including the tab strip.
    bool contains(int x, int y) const
    {
        return mBoxW > 0 && x >= mSdlX && y >= mSdlY && x < mSdlX + mBoxW && y < mSdlY + mBoxH;
    }

    /// Tab index under the SDL point, or -1.
    int hitTab(int x, int y) const
    {
        for (int i = 0; i < kTabs; ++i)
        {
            const Button &tab = mTabs[static_cast<size_t>(i)];
            if (x >= tab.x && y >= tab.y && x < tab.x + tab.w && y < tab.y + tab.h)
            {
                return i;
            }
        }
        return -1;
    }

    /// Page button under the SDL point, or -1.
    int hitButton(int x, int y) const
    {
        for (int i = 0; i < kButtons; ++i)
        {
            const Button &button = mButtons[static_cast<size_t>(i)];
            if (!button.used)
            {
                continue;
            }
            if (x >= button.x && y >= button.y && x < button.x + button.w && y < button.y + button.h)
            {
                return i;
            }
        }
        return -1;
    }

    /// Drops page labels and buttons. Tabs stay.
    void clearContent()
    {
        for (int i = 0; i < kButtons; ++i)
        {
            mButtons[static_cast<size_t>(i)].used = false;
        }
        for (int i = 0; i < kTexts; ++i)
        {
            mTextUsed[static_cast<size_t>(i)] = false;
        }
    }

    /// Places one page label. centerX and centerY are GL pixels.
    /// \param font Pixel height of the glyphs.
    void setText(int slot, const std::string &text, float font, float centerX, float centerY, const char *cacheKey)
    {
        if (slot < 0 || slot >= kTexts)
        {
            return;
        }
        mTextUsed[static_cast<size_t>(slot)] = true;
        mTexts[static_cast<size_t>(slot)]->drawTextCentered(text, font, centerX, centerY, 0xFFFFFFFFu, cacheKey);
    }

    /// `x` and `y` are the top-left corner in SDL coordinates. `fill` is AABBGGRR.
    void setButton(int slot, int x, int y, int w, int h, const std::string &label, float font, const char *cacheKey,
                   uint32_t fill = 0xFFFFFF40u)
    {
        if (slot < 0 || slot >= kButtons)
        {
            return;
        }
        Button &button = mButtons[static_cast<size_t>(slot)];
        button.x = x;
        button.y = y;
        button.w = w;
        button.h = h;
        button.used = true;
        const int glY = mScreenH - y - h;
        mButtonBoxes[static_cast<size_t>(slot)]->drawRectangle(x, glY, w, h, fill);
        mButtonLabels[static_cast<size_t>(slot)]->drawTextCentered(label, font, static_cast<float>(x + w / 2),
                                                                   static_cast<float>(glY + h / 2), 0xFFFFFFFFu,
                                                                   cacheKey);
    }

    /// Draws the window, the tab strip, then the page buttons and labels.
    void render()
    {
        const int glY = mScreenH - mSdlY - mBoxH;
        mBox->drawRectangle(mSdlX, glY, mBoxW, mBoxH, 0x00000099u);
        mTabStrip->drawRectangle(mSdlX, glY, mTabW, mBoxH, 0x00000055u);
        rebuildTabs();

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const glm::mat4 identity(1.0f);
        mBox->setTransformationMatrix(identity);
        mBox->render();
        mTabStrip->setTransformationMatrix(identity);
        mTabStrip->render();
        for (int i = 0; i < kTabs; ++i)
        {
            mTabBoxes[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mTabBoxes[static_cast<size_t>(i)]->render();
            mTabLabels[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mTabLabels[static_cast<size_t>(i)]->render();
        }
        for (int i = 0; i < kButtons; ++i)
        {
            if (!mButtons[static_cast<size_t>(i)].used)
            {
                continue;
            }
            mButtonBoxes[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mButtonBoxes[static_cast<size_t>(i)]->render();
            mButtonLabels[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mButtonLabels[static_cast<size_t>(i)]->render();
        }
        for (int i = 0; i < kTexts; ++i)
        {
            if (!mTextUsed[static_cast<size_t>(i)])
            {
                continue;
            }
            mTexts[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mTexts[static_cast<size_t>(i)]->render();
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

private:
    struct Button
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        bool used = false;
    };

    void rebuildTabs()
    {
        const float font = static_cast<float>(std::clamp(mTabH * 2 / 5, 16, 28));
        for (int i = 0; i < kTabs; ++i)
        {
            Button &tab = mTabs[static_cast<size_t>(i)];
            tab.x = mSdlX + mTabPad;
            tab.y = mSdlY + mTabPad + i * (mTabH + mTabGap);
            tab.w = mTabW - 2 * mTabPad;
            tab.h = mTabH;
            tab.used = true;
            const int glY = mScreenH - tab.y - tab.h;
            const uint32_t fill = (i == mActiveTab) ? 0xC8C8C8D0u : 0xFFFFFF40u;
            mTabBoxes[static_cast<size_t>(i)]->drawRectangle(tab.x, glY, tab.w, tab.h, fill);
            const char *label = mTabNames[static_cast<size_t>(i)].c_str();
            const std::string key = std::string("efis-tab-") + std::to_string(i) + "-" + label;
            mTabLabels[static_cast<size_t>(i)]->drawTextCentered(label, font, static_cast<float>(tab.x + tab.w / 2),
                                                                 static_cast<float>(glY + tab.h / 2), 0xFFFFFFFFu,
                                                                 key.c_str());
        }
    }

    Screen &mScreen;
    std::unique_ptr<Render2D> mBox;
    std::unique_ptr<Render2D> mTabStrip;
    std::array<std::unique_ptr<Render2D>, kTabs> mTabBoxes;
    std::array<std::unique_ptr<Render2D>, kTabs> mTabLabels;
    std::array<std::string, kTabs> mTabNames{};
    std::array<Button, kTabs> mTabs{};
    std::array<std::unique_ptr<Render2D>, kTexts> mTexts;
    std::array<bool, kTexts> mTextUsed{};
    std::array<std::unique_ptr<Render2D>, kButtons> mButtonBoxes;
    std::array<std::unique_ptr<Render2D>, kButtons> mButtonLabels;
    std::array<Button, kButtons> mButtons{};
    int mScreenW = 0;
    int mScreenH = 0;
    int mSdlX = 0;
    int mSdlY = 0;
    int mBoxW = 0;
    int mBoxH = 0;
    int mTabW = 0;
    int mTabH = 0;
    int mTabPad = 8;
    int mTabGap = 6;
    int mContentX = 0;
    int mContentY = 0;
    int mContentW = 0;
    int mContentH = 0;
    int mActiveTab = 0;
};

#endif
