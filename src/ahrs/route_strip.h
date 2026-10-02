/// \file route_strip.h
/// Leg, distance, and time strip drawn under the attitude display.

#ifndef ROUTE_STRIP_H
#define ROUTE_STRIP_H

#include "idata_manager.h"
#include "iwidget.h"
#include "render2d.h"
#include "shader.h"
#include <memory>
#include <string>

class FlightPlan;

/// FROM, TO, and the distance / ETE / ETA columns centered under the AHRS.
class RouteStrip : public IWidget
{
public:
    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param data Situation source. Must outlive this object.
    RouteStrip(Frame &frame, IDataManager &data);

    /// Uses the shared plan's armed leg for FROM / TO. Null returns the strip to
    /// the built-in EPMR -> EPWR default.
    /// \param plan Optional plan. Must outlive this object.
    void setFlightPlan(const FlightPlan *plan) { mPlan = plan; }

    /// Draws the plate, the leg, and the three readouts.
    void render() override;

    /// Unused. The strip stays centered under the attitude display.
    void setPos(int x, int y) override;

    /// Anchors the strip to the bottom of the left column in MODE -> SPLIT.
    void setSplit(bool split) override { mSplit = split; }

private:
    struct Glyph
    {
        std::unique_ptr<Render2D> draw;
        std::string text;
        float size = 0.0f;
    };

    void prepareColors();
    void paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size, uint32_t color,
                    float x, float y);

    IDataManager &mData;
    const FlightPlan *mPlan = nullptr;
    Shader mShader;
    bool mColorsReady = false;
    bool mSplit = false;

    Glyph mFrom;
    Glyph mTo;
    Glyph mDistVal;
    Glyph mEteVal;
    Glyph mEtaVal;
    Glyph mDistLbl;
    Glyph mEteLbl;
    Glyph mEtaLbl;
};

#endif
