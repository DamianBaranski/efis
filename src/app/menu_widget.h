/// \file menu_widget.h
/// MODE, MAP, AIP, ENR, and CONF cells.

#ifndef MENU_WIDGET_H
#define MENU_WIDGET_H

#include "iwidget.h"
#include "render2d.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class AppController;
class SettingsPopup;

/// Column menu. A tap shows it for three seconds. A double tap keeps it until a choice.
class MenuWidget : public IWidget
{
public:
    /// Builds the cell sprites. Hidden until the first tap.
    /// \param frame Loop that draws this menu under the stats overlay.
    /// \param controller Mode flags the cells read and write.
    MenuWidget(Frame &frame, AppController &controller);

    /// Points later taps at the GENERAL window. Call before the loop runs.
    void setPopup(SettingsPopup &popup);

    /// Expires the timeout and draws the cells while the menu is open.
    void render() override;

    /// Opens the menu, or applies the cell under the tap.
    bool mouseClick(int x, int y) override;

    /// Unused. The cells span the top of the window.
    void setPos(int, int) override {}

private:
    struct MenuItem
    {
        const char *label;
        int col;
        int row;
        bool header;
    };

    static constexpr int kMenuCols = 5;
    static constexpr int kMenuRows = 5;
    static constexpr uint64_t kMenuTimeoutMs = 3000;
    static constexpr uint64_t kDoubleTapMs = 450;
    static constexpr MenuItem kItems[] = {
        {"MODE", 0, 0, true}, {"AHRS", 0, 1, false}, {"3D", 0, 2, false},
        {"2D", 0, 3, false},  {"PLANNING", 0, 4, false},
        {"MAP", 1, 0, true},  {"SATT", 1, 1, false}, {"SMPL", 1, 2, false},
        {"AIP", 2, 0, true},  {"3D", 2, 1, false},   {"TEXT", 2, 2, false}, {"VRP", 2, 3, false},
        {"OBSTCL", 2, 4, false},
        {"ENR", 3, 0, true},  {"CHRTS", 3, 1, false}, {"FLP", 3, 2, false}, {"NRST", 3, 3, false},
        {"WTHR", 3, 4, false},
        {"CONF", 4, 0, true}, {"STATS", 4, 1, false}, {"GENERAL", 4, 2, false},
    };
    static constexpr int kItemCount = static_cast<int>(sizeof(kItems) / sizeof(kItems[0]));

    void showMenu();
    void bumpMenuTimeout();
    void expireMenu();
    bool itemActive(int index) const;
    void activateItem(int index);
    void layoutMenu();
    void cellRect(int col, int row, int &x, int &glY) const;
    void rebuildMenuSprites();
    int hitMenuItem(int x, int y) const;
    bool menuContains(int x, int y) const;

    AppController &mController;
    SettingsPopup *mPopup = nullptr;
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
