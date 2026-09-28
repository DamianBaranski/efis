/// \file hsi_widget.h
/// Transparent horizontal situation indicator for the synthetic-vision view.

#ifndef HSI_WIDGET_H
#define HSI_WIDGET_H

#include "idata_manager.h"
#include "iwidget.h"
#include "render2d.h"
#include "shader.h"
#include <memory>
#include <string>

class FlightPlan;

/// AV-30 style HSI. A fit test draws four copies, two along each side.
/// The bezel and the black disc outside the rose are omitted. A dim plate
/// inside the rose keeps the readouts legible over the terrain.
class HsiWidget : public IWidget
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
    /// \param slot Test placement. Four copies cover both sides.
    HsiWidget(Frame &frame, IDataManager &data, Slot slot);

    /// Points this HSI at the shared flight plan's armed leg. When the plan is
    /// not armed the fallback EPWR waypoint is used.
    /// \param plan Optional plan. May be null to detach. Must outlive this object.
    void setFlightPlan(const FlightPlan *plan) { mPlan = plan; }

    /// Draws the rose, deviation bars, and the four readouts.
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
    void rebuildDial(float headingDeg, float bearingDeg, float xteNm);
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
};

#endif
