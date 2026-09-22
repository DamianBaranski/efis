#ifndef POPUP_H
#define POPUP_H

#include "render2d.h"
#include "screen.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

/// Transparent panel used by every settings window.
/// `kTexts` is the number of labels. `kButtons` is the number of hit targets.
/// SDL coordinates have the origin at the top left. Drawing uses GL, origin at the bottom left.
template <int kTexts, int kButtons>
class Popup
{
public:
    explicit Popup(Screen &screen) : mScreen(screen)
    {
        mBox = std::make_unique<Render2D>(screen);
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

    /// Large centered panel. Call again when the screen size changes.
    void layout(int screenW, int screenH)
    {
        mScreenW = std::max(1, screenW);
        mScreenH = std::max(1, screenH);
        const int margin = std::max(16, mScreenH / 30);
        mBoxW = std::min(mScreenW - 2 * margin, std::max(720, mScreenW * 3 / 4));
        mBoxH = std::min(mScreenH - 2 * margin, std::max(420, mScreenH * 3 / 5));
        mSdlX = (mScreenW - mBoxW) / 2;
        mSdlY = (mScreenH - mBoxH) / 2;
    }

    int sdlX() const { return mSdlX; }
    int sdlY() const { return mSdlY; }
    int width() const { return mBoxW; }
    int height() const { return mBoxH; }
    int screenHeight() const { return mScreenH; }

    bool contains(int x, int y) const
    {
        return mBoxW > 0 && x >= mSdlX && y >= mSdlY && x < mSdlX + mBoxW && y < mSdlY + mBoxH;
    }

    /// Returns the button index under the point, or -1.
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

    void setText(int slot, const std::string &text, float font, float centerX, float centerY, const char *cacheKey)
    {
        if (slot < 0 || slot >= kTexts)
        {
            return;
        }
        mTexts[static_cast<size_t>(slot)]->drawTextCentered(text, font, centerX, centerY, 0xFFFFFFFFu, cacheKey);
    }

    /// `x` and `y` are the top-left corner in SDL coordinates.
    void setButton(int slot, int x, int y, int w, int h, const std::string &label, float font, const char *cacheKey)
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
        mButtonBoxes[static_cast<size_t>(slot)]->drawRectangle(x, glY, w, h, 0xFFFFFF40u);
        mButtonLabels[static_cast<size_t>(slot)]->drawTextCentered(label, font, static_cast<float>(x + w / 2),
                                                                   static_cast<float>(glY + h / 2), 0xFFFFFFFFu,
                                                                   cacheKey);
    }

    void render()
    {
        const int glY = mScreenH - mSdlY - mBoxH;
        mBox->drawRectangle(mSdlX, glY, mBoxW, mBoxH, 0x00000099u);

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        const glm::mat4 identity(1.0f);
        mBox->setTransformationMatrix(identity);
        mBox->render();
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

    Screen &mScreen;
    std::unique_ptr<Render2D> mBox;
    std::array<std::unique_ptr<Render2D>, kTexts> mTexts;
    std::array<std::unique_ptr<Render2D>, kButtons> mButtonBoxes;
    std::array<std::unique_ptr<Render2D>, kButtons> mButtonLabels;
    std::array<Button, kButtons> mButtons{};
    int mScreenW = 0;
    int mScreenH = 0;
    int mSdlX = 0;
    int mSdlY = 0;
    int mBoxW = 0;
    int mBoxH = 0;
};

#endif
