/// \file dg_gps_widget.h
/// Transparent AV-30 DG GPS overlay for the synthetic-vision view.

#ifndef DG_GPS_WIDGET_H
#define DG_GPS_WIDGET_H

#include "idata_manager.h"
#include "iwidget.h"
#include "render2d.h"
#include "shader.h"
#include <memory>
#include <string>

class FlightPlan;

/// Heading-up directional gyro with GPS waypoint steering.
/// Same side slots as the HSI. Bezel omitted. A dim plate inside the glass
/// keeps the airplane, needle, and readouts legible over the terrain.
class DgGpsWidget : public IWidget
{
public:
    /// Which side stack this copy occupies. Bottom is the lower dial.
    enum class Slot
    {
        LeftBottom,
        LeftTop,
        RightBottom,
        RightTop
    };

    /// Subscribes to nothing. The picture is read from the situation source each frame.
    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param data Situation source. Must outlive this object.
    /// \param slot Test placement. Matches the HSI side stacks.
    DgGpsWidget(Frame &frame, IDataManager &data, Slot slot);

    /// Points this dial at the shared flight plan's armed leg. When the plan is
    /// not armed the fallback EPWR waypoint is used.
    /// \param plan Optional plan. May be null to detach. Must outlive this object.
    void setFlightPlan(const FlightPlan *plan) { mPlan = plan; }

    /// Draws the rose, bearing needle, track bug, airplane, and the four readouts.
    void render() override;

    /// Unused. The dial stays in its side slot.
    void setPos(int x, int y) override;

private:
    struct Glyph
    {
        std::unique_ptr<Render2D> draw;
        std::string text;
        float size = 0.0f;
    };

    void prepareColors();
    void layout();
    void rebuildDial(float headingDeg, float courseDeg, float trackDeg, float xteNm);
    void paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size, uint32_t color,
                    float x, float y, float rotRad);

    IDataManager &mData;
    const FlightPlan *mPlan = nullptr;
    Slot mSlot;
    Shader mShader;
    bool mColorsReady = false;
    int mLayoutW = 0;
    int mLayoutH = 0;
    float mCx = 0.0f;
    float mCy = 0.0f;
    float mScale = 1.0f;

    Glyph mRose[12];
    Glyph mWptLbl;
    Glyph mWptVal;
    Glyph mBrgLbl;
    Glyph mBrgVal;
    Glyph mDistLbl;
    Glyph mDistVal;
    Glyph mXteLbl;
    Glyph mXteVal;
    Glyph mPage;
    Glyph mBat;
};

#endif
