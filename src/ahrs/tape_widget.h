/// \file tape_widget.h
/// Airspeed and altitude tapes beside the aircraft symbol.

#ifndef TAPE_WIDGET_H
#define TAPE_WIDGET_H

#include "idata_manager.h"
#include "iwidget.h"
#include "render2d.h"
#include "shader.h"
#include <memory>
#include <string>

/// Speed tape on the left of the aircraft symbol, altitude and vertical speed
/// on the right. The speed and altitude arrows stay on the wing line of
/// layer11.png, which is drawn at the screen center and does not bank.
class TapeWidget : public IWidget
{
public:
    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param data Situation source. Must outlive this object.
    TapeWidget(Frame &frame, IDataManager &data);

    /// Draws both tapes, the vertical-speed strip, and the readouts.
    void render() override;

    /// Unused. The tapes stay locked to the aircraft symbol.
    void setPos(int x, int y) override;

private:
    struct Glyph
    {
        std::unique_ptr<Render2D> draw;
        std::string text;
        float size = 0.0f;
    };

    static constexpr int kNums = 16;

    void prepareColors();
    void paintGlyph(Glyph &glyph, const std::string &cacheId, const std::string &text, float size, uint32_t color,
                    float x, float y);

    IDataManager &mData;
    Shader mShader;
    bool mColorsReady = false;

    Glyph mIas;
    Glyph mSpdTop;
    Glyph mAltTop;
    Glyph mTasLbl;
    Glyph mTasVal;
    Glyph mGsLbl;
    Glyph mGsVal;
    Glyph mAlt;
    Glyph mBaroLbl;
    Glyph mBaroVal;
    Glyph mVs;
    Glyph mSpdNum[kNums];
    Glyph mAltNum[kNums];
    bool mSpdUse[kNums]{};
    bool mAltUse[kNums]{};
};

#endif
