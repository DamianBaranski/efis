/// \file tab_window.h
/// GENERAL window: a page list, a row area, and a softkey bar.
#ifndef TAB_WINDOW_H
#define TAB_WINDOW_H

#include "render2d.h"
#include "screen.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

/// Page list on the left, rows in the middle, softkeys along the bottom.
/// SDL coordinates have the origin at the top left. Drawing uses GL, origin at the bottom left.
template <int kTabs, int kTexts, int kButtons>
class TabWindow
{
public:
    static constexpr int kPlates = 8;

    /// Allocates the page list and the page drawables. Call layout before use.
    /// \param screen Window whose height converts SDL coordinates to GL.
    explicit TabWindow(Screen &screen) : mScreen(screen)
    {
        mBox = std::make_unique<Render2D>(screen);
        mTitleBar = std::make_unique<Render2D>(screen);
        mSoftBar = std::make_unique<Render2D>(screen);
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
        for (int i = 0; i < kPlates; ++i)
        {
            mPlateBoxes[static_cast<size_t>(i)] = std::make_unique<Render2D>(screen);
        }
    }

    /// Sizes the window, the page list, and the softkey bar.
    /// \param screenW Window width in pixels.
    /// \param screenH Window height in pixels.
    /// \param topLimit Unused. The window fills the screen.
    void layout(int screenW, int screenH, int topLimit = 0)
    {
        (void)topLimit;
        mScreenW = std::max(1, screenW);
        mScreenH = std::max(1, screenH);
        mSdlX = 0;
        mSdlY = 0;
        mBoxW = mScreenW;
        mBoxH = mScreenH;
        mTitleH = std::clamp(mScreenH / 10, 108, 128);
        mSoftH = std::clamp(mScreenH / 10, 72, 96);
        mPageW = std::clamp(mScreenW / 4, 168, 280);
        if (mPageW > mScreenW / 2)
        {
            mPageW = std::max(120, mScreenW / 2);
        }
        mContentX = mPageW;
        mContentY = mTitleH;
        mContentW = std::max(1, mBoxW - mPageW);
        mContentH = std::max(1, mBoxH - mTitleH - mSoftH);
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
    /// Title-bar height in pixels.
    int titleHeight() const { return mTitleH; }
    /// Left edge of the page body. SDL pixels.
    int contentX() const { return mContentX; }
    /// Top edge of the page body, SDL pixels.
    int contentY() const { return mContentY; }
    /// Page body width in pixels.
    int contentW() const { return mContentW; }
    /// Page body height in pixels.
    int contentH() const { return mContentH; }
    /// Top of the softkey bar, SDL pixels.
    int softY() const { return mScreenH - mSoftH; }
    /// Softkey bar height in pixels.
    int softH() const { return mSoftH; }
    /// Index of the open page.
    int activeTab() const { return mActiveTab; }

    /// Text drawn on one page. Null is ignored.
    void setTabLabel(int slot, const char *label)
    {
        if (slot < 0 || slot >= kTabs || label == nullptr)
        {
            return;
        }
        mTabNames[static_cast<size_t>(slot)] = label;
    }

    /// Highlights one page and leaves the body for the caller to refill.
    void setActiveTab(int slot)
    {
        if (slot < 0 || slot >= kTabs)
        {
            return;
        }
        mActiveTab = slot;
        rebuildTabs();
    }

    /// True when the SDL point is inside the window.
    bool contains(int x, int y) const
    {
        return mBoxW > 0 && x >= mSdlX && y >= mSdlY && x < mSdlX + mBoxW && y < mSdlY + mBoxH;
    }

    /// Page index under the SDL point, or -1.
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

    /// Softkey under the SDL point, or -1.
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

    /// Row plate under the SDL point, or -1.
    int hitPlate(int x, int y) const
    {
        for (int i = 0; i < kPlates; ++i)
        {
            const Button &plate = mPlates[static_cast<size_t>(i)];
            if (!plate.used)
            {
                continue;
            }
            if (x >= plate.x && y >= plate.y && x < plate.x + plate.w && y < plate.y + plate.h)
            {
                return i;
            }
        }
        return -1;
    }

    /// Drops page labels, row plates, and softkeys. The page list stays.
    void clearContent()
    {
        for (int i = 0; i < kButtons; ++i)
        {
            mButtons[static_cast<size_t>(i)].used = false;
        }
        for (int i = 0; i < kPlates; ++i)
        {
            mPlates[static_cast<size_t>(i)].used = false;
        }
        for (int i = 0; i < kTexts; ++i)
        {
            mTextUsed[static_cast<size_t>(i)] = false;
        }
    }

    /// Places one page label. `x` and `y` are the bottom-left corner in GL pixels.
    void setTextLeft(int slot, const std::string &text, float font, float x, float y, uint32_t color,
                     const char *cacheKey)
    {
        if (slot < 0 || slot >= kTexts || text.empty())
        {
            return;
        }
        mTextUsed[static_cast<size_t>(slot)] = true;
        mTexts[static_cast<size_t>(slot)]->drawText(text, font, x, y, color, cacheKey == nullptr ? "" : cacheKey);
    }

    /// Centered page label. centerX and centerY are GL pixels.
    void setText(int slot, const std::string &text, float font, float centerX, float centerY, const char *cacheKey)
    {
        if (slot < 0 || slot >= kTexts)
        {
            return;
        }
        mTextUsed[static_cast<size_t>(slot)] = true;
        mTexts[static_cast<size_t>(slot)]->drawTextCentered(text, font, centerX, centerY, 0xFFFFFFFFu, cacheKey);
    }

    /// Row plate. `x` and `y` are the top-left corner in SDL coordinates.
    /// `fill` and `edge` are RRGGBBAA.
    void setPlate(int slot, int x, int y, int w, int h, uint32_t fill, uint32_t edge)
    {
        if (slot < 0 || slot >= kPlates || w < 1 || h < 1)
        {
            return;
        }
        Button &plate = mPlates[static_cast<size_t>(slot)];
        plate.x = x;
        plate.y = y;
        plate.w = w;
        plate.h = h;
        plate.used = true;
        const int glY = mScreenH - y - h;
        mPlateBoxes[static_cast<size_t>(slot)]->drawFramedRectangle(x, glY, w, h, fill, edge, 2);
    }

    /// Softkey. `x` and `y` are the top-left corner in SDL coordinates.
    /// `fill` and `edge` are RRGGBBAA.
    void setButton(int slot, int x, int y, int w, int h, const std::string &label, float font, const char *cacheKey,
                   uint32_t fill = 0x131920F0u, uint32_t edge = 0x273444FFu)
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
        mButtonBoxes[static_cast<size_t>(slot)]->drawFramedRectangle(x, glY, w, h, fill, edge, 2);
        mButtonLabels[static_cast<size_t>(slot)]->drawTextCentered(label, font, static_cast<float>(x + w / 2),
                                                                   static_cast<float>(glY + h / 2), 0xFFFFFFFFu,
                                                                   cacheKey);
    }

    /// Draws the window, the page list, the row plates, then the softkeys and labels.
    void render()
    {
        const int glY = mScreenH - mSdlY - mBoxH;
        mBox->drawRectangle(mSdlX, glY, mBoxW, mBoxH, 0x101820E0u);
        mTitleBar->drawRectangle(0, mScreenH - mTitleH, mBoxW, mTitleH, 0x090C10F0u);
        mSoftBar->drawRectangle(0, 0, mBoxW, mSoftH, 0x090C10F0u);
        mTabStrip->drawRectangle(0, mSoftH, mPageW, mContentH, 0x090C10F0u);
        rebuildTabs();

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const glm::mat4 identity(1.0f);
        mBox->setTransformationMatrix(identity);
        mBox->render();
        mTitleBar->setTransformationMatrix(identity);
        mTitleBar->render();
        mSoftBar->setTransformationMatrix(identity);
        mSoftBar->render();
        mTabStrip->setTransformationMatrix(identity);
        mTabStrip->render();
        for (int i = 0; i < kTabs; ++i)
        {
            mTabBoxes[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mTabBoxes[static_cast<size_t>(i)]->render();
            mTabLabels[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mTabLabels[static_cast<size_t>(i)]->render();
        }
        for (int i = 0; i < kPlates; ++i)
        {
            if (!mPlates[static_cast<size_t>(i)].used)
            {
                continue;
            }
            mPlateBoxes[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mPlateBoxes[static_cast<size_t>(i)]->render();
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
        const int tabH = std::max(1, mContentH / std::max(1, kTabs));
        const float font =
            static_cast<float>(std::clamp(std::min(tabH / 3, mPageW / 8), 14, 24));
        for (int i = 0; i < kTabs; ++i)
        {
            Button &tab = mTabs[static_cast<size_t>(i)];
            tab.x = 0;
            tab.y = mTitleH + i * tabH;
            tab.w = mPageW;
            tab.h = (i == kTabs - 1) ? (mContentY + mContentH - tab.y) : tabH;
            tab.used = true;
            const int glY = mScreenH - tab.y - tab.h;
            const bool on = i == mActiveTab;
            const uint32_t fill = on ? 0x152030FFu : 0x090C10F0u;
            mTabBoxes[static_cast<size_t>(i)]->drawRectangle(tab.x, glY, tab.w, tab.h, fill);
            const char *label = mTabNames[static_cast<size_t>(i)].c_str();
            const std::string key = std::string("efis-page-") + std::to_string(i) + "-" + label + (on ? "-on" : "-off");
            // Text colors are 0xBBGGRRAA. Muted ink 0x8899A8 is passed as 0xA89988FF.
            const uint32_t ink = on ? 0xFFFFFFFFu : 0xA89988FFu;
            mTabLabels[static_cast<size_t>(i)]->drawTextCentered(label, font, static_cast<float>(tab.x + tab.w / 2),
                                                                 static_cast<float>(glY + tab.h / 2), ink,
                                                                 key.c_str());
        }
    }

    Screen &mScreen;
    std::unique_ptr<Render2D> mBox;
    std::unique_ptr<Render2D> mTitleBar;
    std::unique_ptr<Render2D> mSoftBar;
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
    std::array<std::unique_ptr<Render2D>, kPlates> mPlateBoxes;
    std::array<Button, kPlates> mPlates{};
    int mScreenW = 0;
    int mScreenH = 0;
    int mSdlX = 0;
    int mSdlY = 0;
    int mBoxW = 0;
    int mBoxH = 0;
    int mPageW = 200;
    int mTitleH = 108;
    int mSoftH = 88;
    int mContentX = 0;
    int mContentY = 0;
    int mContentW = 0;
    int mContentH = 0;
    int mActiveTab = 0;
};

#endif
