/// \file split_layout.h
/// Rectangles for MODE -> SPLIT: attitude above the route strip, map on the right.

#ifndef SPLIT_LAYOUT_H
#define SPLIT_LAYOUT_H

#include <algorithm>

/// One rectangle. Origin is the top left, in layout pixels.
struct PaneRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

/// Where the split picture puts each existing widget.
struct SplitLayout
{
    PaneRect ahrs;     ///< Attitude and the speed and altitude tapes.
    PaneRect ahrsClip; ///< Sky and ground, including the band behind the route strip.
    PaneRect route;    ///< Route strip along the bottom of the left column.
    PaneRect map;      ///< Moving map filling the right side.
};

/// Builds the split rectangles for the current window.
/// The left column is the attitude and the route strip. The right side is the map.
inline SplitLayout makeSplitLayout(int screenW, int screenH)
{
    screenW = std::max(1, screenW);
    screenH = std::max(1, screenH);
    const int leftW = std::max(1, screenW / 2);
    const int stripH = std::clamp(screenH / 7, std::min(64, screenH / 3), std::min(120, screenH / 2));

    SplitLayout layout;
    layout.ahrs = PaneRect{0, 0, leftW, std::max(1, screenH - stripH)};
    layout.ahrsClip = PaneRect{0, 0, leftW, screenH};
    layout.route = PaneRect{0, screenH - stripH, leftW, stripH};
    layout.map = PaneRect{leftW, 0, std::max(1, screenW - leftW), screenH};
    return layout;
}

#endif
