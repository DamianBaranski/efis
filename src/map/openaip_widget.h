/// \file openaip_widget.h
/// Flat map widget. The 3D view does not construct it.
#ifndef OPENAIP_WIDGET_H
#define OPENAIP_WIDGET_H

#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "openaip_client.h"
#include "render2d.h"
#include <cstdint>
#include <map>
#include <memory>

/// Flat OpenAIP mosaic. Not constructed by main(); the 3D view uses SatClipmap.
class OpenAipWidget : public IWidget, public IObserver<DataType>
{
public:
    /// Subscribes to position and prepares the tile grid.
    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param dataManager Situation source. Must outlive this widget.
    OpenAipWidget(Frame &frame, IDataManager &dataManager);
    /// Draws the cached tiles and the ownship mark.
    void render() override;
    /// Unused. The mosaic fills the window.
    void setPos(int x, int y) override;
    /// Recenters the mosaic when position changes.
    void update(DataType type) override;
    /// Shows or hides the mosaic.
    void enable(bool enable) override;

private:
    uint64_t tileKey(int x, int y) const;
    void ensureTile(int tileX, int tileY);
    void renderOwnship();

    static constexpr int kZoom = 13;
    static constexpr int kRadius = 7;

    IDataManager &mDataManager;
    LocationData mLocation{};
    Render2D mOwnship;
    std::map<uint64_t, std::unique_ptr<Render2D>> mTiles;
};

#endif
