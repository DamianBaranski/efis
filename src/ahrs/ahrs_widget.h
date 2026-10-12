/// \file ahrs_widget.h
/// One attitude instrument for AHRS mode and 3D mode.

#ifndef AHRS_WIDGET
#define AHRS_WIDGET

#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "data_type.h"
#include "render2d.h"
#include <array>

/// Pitch, roll, slip, and the aircraft symbol stay up in both AHRS and 3D.
/// The sky and ground tape is drawn only in AHRS mode.
class AhrsWidget : public IObserver<DataType>, public IWidget
{
public:
    /// Registers with the frame and subscribes to attitude updates.
    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param dataManager Situation source. Must outlive this object.
    AhrsWidget(Frame &frame, IDataManager &dataManager);

    /// Draws pitch, roll, slip, and the aircraft symbol.
    void render() override;

    /// Places the instrument. Coordinates are pixels, origin at the top left.
    /// \param x Left edge.
    /// \param y Top edge.
    void setPos(int x, int y) override;

    /// Copies a new attitude sample. Other channels are ignored.
    /// \param type Situation channel that changed.
    void update(DataType type) override;

    /// Shows or hides the sky and ground tape. The aircraft symbol is unaffected.
    void setDrawSkyGround(bool draw) { mDrawSkyGround = draw; }

    /// Draws inside the left column of MODE -> SPLIT. Full screen when false.
    void setSplit(bool split) override;

private:
    void rebuildSprites();
    void buildRulers(float cx, float cy, float scale);
    /// Whole pitch bars from -90° to +90° whose degree is within 30° of `pitchDeg`.
    void buildVisibleLadder(float pitchDeg);
    float hudScale() const;

    /// \brief Updates the internal renderers based on the new attitude data.
    void updateRenderers();

    IDataManager &mDataManager;            ///< Reference to the data manager providing attitude data.
    Render2D mLandRepresentation;  ///< Representation of land in the AHRS widget.
    Render2D mHorizonLine;         ///< Horizon line, drawn as geometry so it stays sharp.
    Render2D mPithScale;           ///< Pitch ladder, drawn as geometry so it stays sharp.
    Render2D mRollPointer;         ///< Fixed bank index, drawn as geometry.
    Render2D mSkipSkidIndicator;   ///< Representation of the skip/skid indicator in the AHRS widget.
    Render2D mAttitudeIndicator;   ///< Bank arc and sky pointer, drawn as geometry.
    Render2D mAircraftSymbol;      ///< Representation of the aircraft symbol in the AHRS widget.
    /// ±10° through ±90° on each side of the ladder.
    static constexpr int cPitchLabelCount = 36;
    std::array<Render2D, cPitchLabelCount> mPitchLabel;
    AttitudeData mAttitudeData;    ///< Attitude data.
    bool mDrawSkyGround = true; ///< Land and horizon tape. False in 3D so terrain shows through.
    bool mSplit = false;        ///< Left column of the split picture.
    int mAttitudeY;
    int mLayoutX = 0;
    int mLayoutW = 0;
    int mLayoutH = 0;
    int mPaneX = -1;
    int mClipX = 0;
    int mClipY = 0;
    int mClipW = 0;
    int mClipH = 0;
    float mHudScale = 1.0f;
    /// Aircraft pitch in degrees. Nose up is positive. The ladder window follows it.
    float mPitchDeg = 0.0f;

    static constexpr const char *cSkipSkidIndicatorTexture = "layer7.png";          ///< Texture file for the skip/skid indicator.
    static constexpr const char *cAircraftSymbolTexture = "layer11.png";            ///< Texture file for the aircraft symbol.
    static constexpr const char *cLandRepresentationTexture = "layer3.png";         ///< Texture file for the land representation.
    static constexpr float cDesignHeight = 600.0f;
    /// The 10° labels sit 62.5 px from the horizon in the unscaled ladder art.
    static constexpr float cPitchPixelsPerDegree = 6.25f;
    static constexpr float cPixelPerPitchRadians = cPitchPixelsPerDegree * (180.0f / static_cast<float>(M_PI));
};

#endif
