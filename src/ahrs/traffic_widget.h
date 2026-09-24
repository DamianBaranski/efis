/// \file traffic_widget.h
/// Heading-up traffic dial for the synthetic-vision view.

#ifndef TRAFFIC_WIDGET_H
#define TRAFFIC_WIDGET_H

#include "idata_manager.h"
#include "iwidget.h"
#include "render2d.h"
#include "shader.h"
#include <memory>
#include <string>

/// AV-30 traffic overlay. Same side slots as the HSI fit test.
/// The compass matches the HSI dial. Selected-aircraft distance and altitude
/// sit on the left, speed and type on the right, registration centered beneath.
class TrafficWidget : public IWidget
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

    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param data Situation source. Must outlive this object.
    /// \param slot Test placement beside the attitude display.
    TrafficWidget(Frame &frame, IDataManager &data, Slot slot);

    /// Draws the rose, range rings, traffic, and the selected-aircraft block.
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
    void paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size, uint32_t color,
                    float x, float y, float rotRad);

    IDataManager &mData;
    Slot mSlot;
    Shader mShader;
    bool mColorsReady = false;
    int mLayoutW = 0;
    int mLayoutH = 0;
    float mCx = 0.0f;
    float mCy = 0.0f;
    float mScale = 1.0f;

    Glyph mRose[12];
    Glyph mRing[3];
    Glyph mRel[3];
    Glyph mSelDist;
    Glyph mSelAlt;
    Glyph mSelGs;
    Glyph mSelType;
    Glyph mSelId;
    bool mRelOn[3]{};
    bool mSelOn = false;
};

#endif
