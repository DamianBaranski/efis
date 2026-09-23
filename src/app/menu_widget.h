/// \file menu_widget.h
/// MODE, MAP, AIP, ENR, and CONF cells.

#ifndef MENU_WIDGET_H
#define MENU_WIDGET_H

#include "iwidget.h"
#include "render2d.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class AppController;
class SettingsPopup;

/// Column menu. A tap shows it for three seconds. A double tap keeps it until a choice.
class MenuWidget : public IWidget
{
public:
    /// Builds the cell actions. Hidden until the first tap.
    /// \param frame Window this menu measures against. The owner adds it to the loop.
    /// \param controller Mode flags the cells read and write.
    /// \param popup GENERAL window the menu opens and hits.
    MenuWidget(Frame &frame, AppController &controller, SettingsPopup &popup);

    /// Expires the timeout and draws the cells while the menu is open.
    void render() override;

    /// Opens the menu, or applies the cell under the tap.
    bool mouseClick(int x, int y) override;

    /// Unused. The cells span the top of the window.
    void setPos(int, int) override {}

private:
    /// One cell. execute is the command run on a tap. active paints the highlight.
    struct Cell
    {
        const char *label;
        int col;
        int row;
        bool header;
        std::function<void()> execute;
        std::function<bool()> active;
    };

    static constexpr int kMenuCols = 5;
    static constexpr int kMenuRows = 5;
    static constexpr uint64_t kMenuTimeoutMs = 3000;
    static constexpr uint64_t kDoubleTapMs = 450;

    void buildCells();
    void showMenu();
    void bumpMenuTimeout();
    void expireMenu();
    void activateItem(int index);
    void layoutMenu();
    void cellRect(int col, int row, int &x, int &glY) const;
    void rebuildMenuSprites();
    int hitMenuItem(int x, int y) const;
    bool menuContains(int x, int y) const;

    AppController &mController;
    SettingsPopup &mPopup;
    std::vector<Cell> mCells;
    bool mVisible = false;
    bool mLocked = false;
    uint64_t mLastTapMs = 0;
    std::chrono::steady_clock::time_point mHideAt{};
    int mMenuW = 0;
    int mMenuH = 0;
    int mPad = 4;
    int mTop = 4;
    int mGap = 4;
    int mBoxW = 80;
    int mBoxH = 40;
    int mFont = 16;
    uint64_t mSeenRevision = 0;
    std::vector<std::unique_ptr<Render2D>> mBoxes;
    std::vector<std::unique_ptr<Render2D>> mLabels;
};

#endif
