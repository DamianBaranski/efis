/// \file menu_widget.h
/// One bookmark that opens the flight-picture side menu.

#ifndef MENU_WIDGET_H
#define MENU_WIDGET_H

#include "iwidget.h"
#include "render2d.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AppController;
class SettingsPopup;

/// Expand mark on the left edge. It opens one full-height translucent side menu.
/// The split map keeps its own settings control.
class MenuWidget : public IWidget
{
public:
    /// Builds the menu actions.
    /// \param frame Window this menu measures against. The owner adds it to the loop.
    /// \param controller Mode flags the rows read and write.
    /// \param popup GENERAL window the menu opens and hits.
    MenuWidget(Frame &frame, AppController &controller, SettingsPopup &popup);

    /// Draws the expand mark and the side menu while it is open.
    void render() override;

    /// Arms a tap. The action runs on release, so the first finger of a pinch does not open the menu.
    bool mouseClick(int x, int y) override;

    /// Commits the armed tap when the finger lifts close to where it went down.
    bool mouseUp(int x, int y) override;

    /// Scrolls the list when a drag starts on a row.
    bool mouseMove(int x, int y, int dx, int dy) override;

    /// Scrolls the list from the wheel.
    bool mouseWheel(int x, int y, int dy) override;

    /// Drops an armed tap. A pinch is not a menu press.
    void gesturePinchBegan() override;

    /// Unused. The expand mark sits on the left edge.
    void setPos(int, int) override {}

    /// True while the side menu is open.
    bool isOpen() const { return mOpen; }

    /// Closes the side menu. Does not notify the map.
    void close();

    /// Called when the side menu opens, so the map menu can close.
    void setOnOpen(std::function<void()> callback) { mOnOpen = std::move(callback); }

private:
    struct Entry
    {
        std::string label;
        bool header = false;
        std::function<std::string()> textOf;
        std::function<void()> run;
        std::function<bool()> selected;
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    enum class Arm
    {
        None,
        Toggle,
        Row,
        Dismiss,
    };

    void buildEntries();
    void measure();
    void ensureSprites();
    void paint();
    Render2D &sprite();
    bool inside(int x, int y, int rx, int ry, int rw, int rh) const;
    int hitRow(int x, int y) const;
    void commit(Arm arm, int index);
    void clearArm();
    static bool near(int x, int y, int ax, int ay);

    AppController &mController;
    SettingsPopup &mPopup;
    std::function<void()> mOnOpen;
    std::vector<Entry> mRows;
    bool mOpen = false;
    Arm mArm = Arm::None;
    int mArmIndex = -1;
    int mArmX = 0;
    int mArmY = 0;

    int mScreenW = 0;
    int mScreenH = 0;
    int mTabX = 0;
    int mTabY = 0;
    int mTabW = 26;
    int mTabH = 72;
    int mPanelX = 0;
    int mPanelY = 0;
    int mPanelW = 0;
    int mPanelH = 0;
    int mFont = 26;
    int mScroll = 0;
    int mContentH = 0;
    int mContentSprites = 0;
    bool mDragList = false;

    int mSpriteCount = 0;
    size_t mSpriteCursor = 0;
    int mBuiltW = -1;
    int mBuiltH = -1;
    uint64_t mBuiltRevision = 0;
    bool mBuiltOpen = false;
    int mBuiltScroll = 0;
    std::vector<std::unique_ptr<Render2D>> mSprites;
};

#endif
