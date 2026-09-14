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

class OpenAipWidget : public IWidget, public IObserver<DataType>
{
public:
    OpenAipWidget(Screen &screen, IDataManager &dataManager);
    void render() override;
    void setPos(int x, int y) override;
    void update(DataType type) override;
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
