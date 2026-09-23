/// \file dropdown.cpp
/// Draws a closed bar and, when open, a column of choices above or below it.
#include "dropdown.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <glm/glm.hpp>

Dropdown::Dropdown(const Screen &screen) : mScreen(screen)
{
    mBar = std::make_unique<Render2D>(screen);
    mBarText = std::make_unique<Render2D>(screen);
    mMark = std::make_unique<Render2D>(screen);
    mList = std::make_unique<Render2D>(screen);
}

void Dropdown::setItems(std::vector<std::string> items, int selected)
{
    mItems = std::move(items);
    if (mItems.empty())
    {
        mSelected = 0;
        mOpen = false;
        return;
    }
    mSelected = std::clamp(selected, 0, static_cast<int>(mItems.size()) - 1);
}

void Dropdown::place(int x, int y, int w, int h)
{
    mX = x;
    mY = y;
    mW = std::max(1, w);
    mH = std::max(1, h);
}

void Dropdown::setFont(float font)
{
    mFont = std::max(8.0f, font);
}

void Dropdown::close()
{
    mOpen = false;
}

void Dropdown::listBounds(bool &up, int &first, int &shown) const
{
    const int count = static_cast<int>(mItems.size());
    up = false;
    first = 0;
    shown = 0;
    if (count <= 0 || mH <= 0)
    {
        return;
    }
    const int screenH = std::max(1, mScreen.getHeight());
    const int below = std::max(0, (screenH - (mY + mH)) / mH);
    const int above = std::max(0, mY / mH);
    up = below < count && above > below;
    const int room = std::max(1, up ? above : below);
    shown = std::min(count, room);
    first = 0;
    if (mSelected >= shown)
    {
        first = mSelected - shown + 1;
    }
    first = std::clamp(first, 0, count - shown);
}

bool Dropdown::contains(int x, int y, int left, int top, int w, int h) const
{
    return x >= left && y >= top && x < left + w && y < top + h;
}

Dropdown::Click Dropdown::mouseClick(int x, int y)
{
    Click click;
    if (mItems.empty() || mW <= 0 || mH <= 0)
    {
        return click;
    }
    bool up = false;
    int first = 0;
    int shown = 0;
    listBounds(up, first, shown);
    if (mOpen)
    {
        for (int i = 0; i < shown; ++i)
        {
            const int top = up ? (mY - shown * mH + i * mH) : (mY + mH + i * mH);
            if (!contains(x, y, mX, top, mW, mH))
            {
                continue;
            }
            mSelected = first + i;
            mOpen = false;
            click.consumed = true;
            click.chosen = mSelected;
            return click;
        }
    }
    if (contains(x, y, mX, mY, mW, mH))
    {
        mOpen = !mOpen;
        click.consumed = true;
        return click;
    }
    if (mOpen)
    {
        mOpen = false;
        click.consumed = true;
    }
    return click;
}

void Dropdown::render()
{
    if (mW <= 0 || mH <= 0)
    {
        return;
    }
    const int screenH = std::max(1, mScreen.getHeight());
    const int barGl = screenH - mY - mH;
    const std::string label = mItems.empty() ? "DEFAULT" : mItems[static_cast<size_t>(mSelected)];
    mBar->drawRectangle(mX, barGl, mW, mH, mOpen ? 0x4DA3FFB0u : 0xFFFFFF40u);
    mBarText->drawTextCentered(label, mFont, static_cast<float>(mX + (mW - mH) / 2),
                               static_cast<float>(barGl + mH / 2), 0xFFFFFFFFu,
                               (std::string("efis-drop-bar-") + label).c_str());
    mMark->drawTextCentered(mOpen ? "^" : "v", mFont, static_cast<float>(mX + mW - mH / 2),
                            static_cast<float>(barGl + mH / 2), 0xFFFFFFFFu, mOpen ? "efis-drop-up" : "efis-drop-down");

    bool up = false;
    int first = 0;
    int shown = 0;
    listBounds(up, first, shown);
    if (mOpen && shown > 0)
    {
        const int listTop = up ? (mY - shown * mH) : (mY + mH);
        const int listGl = screenH - listTop - shown * mH;
        mList->drawRectangle(mX, listGl, mW, shown * mH, 0x101018F0u);
        if (static_cast<int>(mRows.size()) < shown)
        {
            mRows.resize(static_cast<size_t>(shown));
            mRowText.resize(static_cast<size_t>(shown));
            for (int i = 0; i < shown; ++i)
            {
                if (!mRows[static_cast<size_t>(i)])
                {
                    mRows[static_cast<size_t>(i)] = std::make_unique<Render2D>(mScreen);
                    mRowText[static_cast<size_t>(i)] = std::make_unique<Render2D>(mScreen);
                }
            }
        }
        for (int i = 0; i < shown; ++i)
        {
            const int index = first + i;
            const int top = up ? (mY - shown * mH + i * mH) : (mY + mH + i * mH);
            const int glY = screenH - top - mH;
            const uint32_t fill = index == mSelected ? 0x4DA3FFB0u : 0xFFFFFF20u;
            mRows[static_cast<size_t>(i)]->drawRectangle(mX, glY, mW, mH, fill);
            const std::string &row = mItems[static_cast<size_t>(index)];
            const std::string key = "efis-drop-row-" + std::to_string(index) + "-" + row;
            mRowText[static_cast<size_t>(i)]->drawTextCentered(row, mFont, static_cast<float>(mX + mW / 2),
                                                               static_cast<float>(glY + mH / 2), 0xFFFFFFFFu, key.c_str());
        }
    }

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const glm::mat4 identity(1.0f);
    mBar->setTransformationMatrix(identity);
    mBar->render();
    mBarText->setTransformationMatrix(identity);
    mBarText->render();
    mMark->setTransformationMatrix(identity);
    mMark->render();
    if (mOpen && shown > 0)
    {
        mList->setTransformationMatrix(identity);
        mList->render();
        for (int i = 0; i < shown; ++i)
        {
            mRows[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mRows[static_cast<size_t>(i)]->render();
            mRowText[static_cast<size_t>(i)]->setTransformationMatrix(identity);
            mRowText[static_cast<size_t>(i)]->render();
        }
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
