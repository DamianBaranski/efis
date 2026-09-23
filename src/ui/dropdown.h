/// \file dropdown.h
/// Closed list that opens a column of choices.

#ifndef DROPDOWN_H
#define DROPDOWN_H

#include "render2d.h"
#include <memory>
#include <string>
#include <vector>

/// One selectable value. Closed, it shows the current label. Open, it lists the choices.
/// Coordinates are SDL pixels, origin at the top left.
class Dropdown
{
public:
    /// Allocates the closed bar. Call setItems and place before the first draw.
    /// \param screen Window whose height flips SDL Y into GL Y.
    explicit Dropdown(const Screen &screen);

    /// Replaces the choices. selected is clamped into range.
    void setItems(std::vector<std::string> items, int selected);
    /// Places the closed bar.
    /// \param x Left edge, SDL pixels.
    /// \param y Top edge, SDL pixels.
    /// \param w Width in pixels.
    /// \param h Height in pixels. Each open row uses the same height.
    void place(int x, int y, int w, int h);
    /// Glyph height in pixels.
    void setFont(float font);
    /// Closes the list. The closed bar stays.
    void close();
    /// True while the choice list is drawn.
    bool isOpen() const { return mOpen; }

    /// Result of one tap.
    struct Click
    {
        bool consumed = false; ///< True when the tap hit the control or closed an open list.
        int chosen = -1;       ///< Index that was picked. -1 when the selection did not change.
    };

    /// Opens, closes, or picks a row.
    /// \param x Pixels from the left.
    /// \param y Pixels from the top.
    Click mouseClick(int x, int y);

    /// Draws the closed bar, and the list when it is open.
    void render();

private:
    void listBounds(bool &up, int &first, int &shown) const;
    bool contains(int x, int y, int left, int top, int w, int h) const;

    const Screen &mScreen;
    std::vector<std::string> mItems;
    int mSelected = 0;
    int mX = 0;
    int mY = 0;
    int mW = 0;
    int mH = 0;
    float mFont = 18.0f;
    bool mOpen = false;
    std::unique_ptr<Render2D> mBar;
    std::unique_ptr<Render2D> mBarText;
    std::unique_ptr<Render2D> mMark;
    std::unique_ptr<Render2D> mList;
    std::vector<std::unique_ptr<Render2D>> mRows;
    std::vector<std::unique_ptr<Render2D>> mRowText;
};

#endif
